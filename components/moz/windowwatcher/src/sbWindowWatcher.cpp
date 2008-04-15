/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 :miv */
/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//
// Songbird window watcher.
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

/**
 * \file  sbWindowWatcher.cpp
 * \brief Songbird Window Watcher Source.
 */

//------------------------------------------------------------------------------
//
// Songbird window watcher imported services.
//
//------------------------------------------------------------------------------

// Self imports.
#include "sbWindowWatcher.h"

// Mozilla imports.
#include <nsAutoLock.h>
#include <nsIDOMDocument.h>
#include <nsIDOMElement.h>
#include <nsIDOMEvent.h>
#include <nsIProxyObjectManager.h>
#include <nsServiceManagerUtils.h>
#include <nsThreadUtils.h>


//------------------------------------------------------------------------------
//
// Songbird window watcher nsISupports implementation.
//
//------------------------------------------------------------------------------

NS_IMPL_THREADSAFE_ISUPPORTS3(sbWindowWatcher,
                              sbIWindowWatcher,
                              nsIObserver,
                              nsISupportsWeakReference)


//------------------------------------------------------------------------------
//
// Songbird window watcher sbIWindowWatcher implementation.
//
//------------------------------------------------------------------------------

/**
 * \brief Call callback specified by aCallback with a window of the type
 *        specified by aWindowType.  Wait until a window of the specified type
 *        is available or until shutdown.  Call callback with null window on
 *        shutdown.  Call callback on main thread.
 *
 * \param aWindowType         Type of window with which to call.
 * \param aCallback           Callback to call with window.
 */

NS_IMETHODIMP
sbWindowWatcher::CallWithWindow(const nsAString&           aWindowType,
                                sbICallWithWindowCallback* aCallback)
{
  // Validate arguments.
  NS_ENSURE_ARG_POINTER(aCallback);

  // Function variables.
  nsresult rv;

  // If not on main thread, call back through a proxy.
  if (!NS_IsMainThread()) {
    // Get a main thread proxy to this instance.
    nsCOMPtr<sbIWindowWatcher> proxyWindowWatcher;
    rv = GetProxiedWindowWatcher(getter_AddRefs(proxyWindowWatcher));
    NS_ENSURE_SUCCESS(rv, rv);

    // Call back through the proxy.
    rv = proxyWindowWatcher->CallWithWindow(aWindowType, aCallback);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Check if window is already available.
  nsCOMPtr<nsIDOMWindow> window;
  rv = GetWindow(aWindowType, getter_AddRefs(window));
  NS_ENSURE_SUCCESS(rv, rv);

  // If a window is available or this instance is shutting down, call the
  // callback.  Otherwise, place the call with window information on the call
  // with window list.
  if (window || mIsShuttingDown) {
    aCallback->HandleWindowCallback(window);
  } else {
    CallWithWindowInfo callWithWindowInfo;
    callWithWindowInfo.windowType = aWindowType;
    callWithWindowInfo.callback = aCallback;
    mCallWithWindowList.AppendElement(callWithWindowInfo);
  }

  return NS_OK;
}


/**
 * \brief Get the top-most available window of the type specified by
 *        aWindowType.  Return null if no matching window is available.  Since
 *        nsIDOMWindow is not thread-safe, this method may only be called on
 *        the main thread.
 *
 * \param aWindowType         Type of window to get.
 *
 * \return                    Window of specified type or null if none
 *                            available.
 */

NS_IMETHODIMP
sbWindowWatcher::GetWindow(const nsAString& aWindowType,
                           nsIDOMWindow**   _retval)
{
  // Validate arguments.
  NS_ENSURE_ARG_POINTER(_retval);

  // Function variables.
  nsCOMPtr<nsIDOMWindow> retWindow;
  PRBool                 success;
  nsresult               rv;

  // This method may only be called on the main thread.
  NS_ENSURE_TRUE(NS_IsMainThread(), NS_ERROR_UNEXPECTED);

  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Get an enumerator of all windows of the specified type, sorted from oldest
  // to youngest.
  nsCOMPtr<nsISimpleEnumerator> enumerator;
  rv = mWindowMediator->GetEnumerator(aWindowType.BeginReading(),
                                      getter_AddRefs(enumerator));
  NS_ENSURE_SUCCESS(rv, rv);

  // Search for the most recently focused ready window of the specified type.  
  // The enumerator enumerates from oldest to youngest (least recently focused
  // to most recently), so the last matching window is the most recently focused
  // one.
  PRBool hasMoreElements;
  rv = enumerator->HasMoreElements(&hasMoreElements);
  NS_ENSURE_SUCCESS(rv, rv);
  while (hasMoreElements) {
    // Get the window.  Skip if not ready.
    nsCOMPtr<nsISupports> _window;
    nsCOMPtr<nsIDOMWindow> window;
    rv = enumerator->GetNext(getter_AddRefs(_window));
    NS_ENSURE_SUCCESS(rv, rv);
    window = do_QueryInterface(_window, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = enumerator->HasMoreElements(&hasMoreElements);
    NS_ENSURE_SUCCESS(rv, rv);

    // Skip window if not ready.
    WindowInfo* windowInfo;
    success = mWindowInfoTable.Get(window, &windowInfo);
    if (!success || !(windowInfo->isReady))
      continue;

    // Get the window type.
    nsAutoString windowType;
    rv = GetWindowType(window, windowType);
    if (NS_FAILED(rv))
      continue;

    // Check for a match.
    if (aWindowType.Equals(windowType)) {
      retWindow = window;
    }
  }

  // Return results.
  NS_IF_ADDREF(*_retval = retWindow);

  return NS_OK;
}


/**
 * \brief Wait until a window of the type specified by aWindowType is
 *        available or until shutdown.  This method may not be called on the
 *        main thread.
 *
 * \param aWindowType         Type of window to get.
 */

NS_IMETHODIMP
sbWindowWatcher::WaitForWindow(const nsAString& aWindowType)
{
  nsresult rv;

  // This method may only be called on the main thread.
  NS_ENSURE_TRUE(!NS_IsMainThread(), NS_ERROR_UNEXPECTED);

  // Don't wait if this instance is shutting down.
  {
    // Check is shutting down within the monitor.
    nsAutoMonitor autoMonitor(mMonitor);
    if (mIsShuttingDown)
      return NS_OK;
  }

  // Create a wait for window object.
  nsRefPtr<sbWindowWatcherWaitForWindow> waitForWindow;
  rv = sbWindowWatcherWaitForWindow::New(getter_AddRefs(waitForWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  // Wait for the window.
  rv = waitForWindow->Wait(aWindowType);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


//
// Getters/setters.
//

/**
 * \brief True if the window watcher is shutting down and no more windows will
 *        become available.
 */

NS_IMETHODIMP
sbWindowWatcher::GetIsShuttingDown(PRBool* aIsShuttingDown)
{
  // Validate arguments.
  NS_ENSURE_ARG_POINTER(aIsShuttingDown);

  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Return results.
  *aIsShuttingDown = mIsShuttingDown;

  return NS_OK;
}


//------------------------------------------------------------------------------
//
// Songbird window watcher nsIObserver implementation.
//
//------------------------------------------------------------------------------

/**
 * Observe will be called when there is a notification for the
 * topic |aTopic|.  This assumes that the object implementing
 * this interface has been registered with an observer service
 * such as the nsIObserverService. 
 *
 * If you expect multiple topics/subjects, the impl is 
 * responsible for filtering.
 *
 * You should not modify, add, remove, or enumerate 
 * notifications in the implemention of observe. 
 *
 * @param aSubject : Notification specific interface pointer.
 * @param aTopic   : The notification topic or subject.
 * @param aData    : Notification specific wide string.
 *                    subject event.
 */

NS_IMETHODIMP
sbWindowWatcher::Observe(nsISupports*     aSubject,
                         const char*      aTopic,
                         const PRUnichar* aData)
{
  nsresult rv;

  /* Dispatch processing of event. */
  if (!strcmp(aTopic, "domwindowopened")) {
    rv = OnDOMWindowOpened(aSubject, aData);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (!strcmp(aTopic, "domwindowclosed")) {
    rv = OnDOMWindowClosed(aSubject, aData);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (!strcmp(aTopic, "quit-application-granted")) {
    rv = OnQuitApplicationGranted();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}


/**
 * Handle the DOM window opened event specified by aSubject and aData.
 *
 * \param aSubject              Event subject.  Can be QI'ed to nsIDOMWindow.
 * \param aData                 Event data.
 */

nsresult
sbWindowWatcher::OnDOMWindowOpened(nsISupports*     aSubject,
                                   const PRUnichar* aData)
{
  // Validate arguments and state.
  NS_ASSERTION(aSubject, "aSubject is null");
  NS_ASSERTION(NS_IsMainThread(), "not on main thread");

  // Function variables.
  nsresult rv;

  // Get the event window.
  nsCOMPtr<nsIDOMWindow> window = do_QueryInterface(aSubject, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Add the window.
  rv = AddWindow(window);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Handle the DOM window closed event specified by aSubject and aData.
 *
 * \param aSubject              Event subject.  Can be QI'ed to nsIDOMWindow.
 * \param aData                 Event data.
 */

nsresult
sbWindowWatcher::OnDOMWindowClosed(nsISupports*     aSubject,
                                   const PRUnichar* aData)
{
  // Validate arguments and state.
  NS_ASSERTION(aSubject, "aSubject is null");
  NS_ASSERTION(NS_IsMainThread(), "not on main thread");

  // Function variables.
  nsresult rv;

  // Get the event window.
  nsCOMPtr<nsIDOMWindow> window = do_QueryInterface(aSubject, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Remove the window.
  rv = RemoveWindow(window);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Handle a quit application granted event.
 */

nsresult
sbWindowWatcher::OnQuitApplicationGranted()
{
  // Validate state.
  NS_ASSERTION(NS_IsMainThread(), "not on main thread");

  // Shutdown the Songbird window watcher services.
  Shutdown();

  return NS_OK;
}


//------------------------------------------------------------------------------
//
// Songbird window watcher services.
//
//------------------------------------------------------------------------------

/**
 * Construct a window watcher instance.
 */

sbWindowWatcher::sbWindowWatcher() :
  mIsShuttingDown(PR_FALSE)
{
}


/**
 * Destroy a window watcher instance.
 */

sbWindowWatcher::~sbWindowWatcher()
{
  // Finalize the window watcher.
  Finalize();
}


/**
 * Initialize the window watcher services.
 */

nsresult
sbWindowWatcher::Init()
{
  nsresult rv;

  // Get the window watcher service.
  mWindowWatcher = do_GetService("@mozilla.org/embedcomp/window-watcher;1",
                                 &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the window mediator service.
  mWindowMediator = do_GetService("@mozilla.org/appshell/window-mediator;1",
                                  &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the observer service.
  mObserverService = do_GetService("@mozilla.org/observer-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create a monitor.
  mMonitor = nsAutoMonitor::NewMonitor("sbWindowWatcher::mMonitor");
  NS_ENSURE_TRUE(mMonitor, NS_ERROR_OUT_OF_MEMORY);

  // Initialize the window information table.
  mWindowInfoTable.Init();

  // Add a window watcher observer.
  rv = mWindowWatcher->RegisterNotification(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // Add quit-application-granted observer.
  rv = mObserverService->AddObserver(this,
                                     "quit-application-granted",
                                     PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Finalize the window watcher services.
 */

void
sbWindowWatcher::Finalize()
{
  // Ensure this instance is shut down.
  Shutdown();

  // Remove all windows.
  RemoveAllWindows();

  // Dispose of monitor.
  if (mMonitor)
    nsAutoMonitor::DestroyMonitor(mMonitor);
  mMonitor = nsnull;

  // Remove object references.
  mWindowWatcher = nsnull;
  mWindowMediator = nsnull;
  mWindowList.Clear();
  mWindowInfoTable.Clear();
  mCallWithWindowList.Clear();
}


//------------------------------------------------------------------------------
//
// Internal Songbird window watcher services.
//
//------------------------------------------------------------------------------

/**
 * Shut down the Songbird window watcher services.  The window watcher will stop
 * watching windows and will return a null window for all waiting method calls
 * and callbacks.  The window watcher will still be usable but will no longer
 * wait for windows.
 */

void
sbWindowWatcher::Shutdown()
{
  // Validate state.
  NS_ASSERTION(NS_IsMainThread(), "not on main thread");

  // Operate within the monitor.
  {
    nsAutoMonitor autoMonitor(mMonitor);

    // Do nothing if already shutting down.
    if (mIsShuttingDown)
      return;

    // Indicate that this instance is shutting down.
    mIsShuttingDown = PR_TRUE;
  }

  // Remove quit-application-granted observer.
  mObserverService->RemoveObserver(this, "quit-application-granted");

  // Invoke all call with window callbacks.
  InvokeCallWithWindowCallbacks(nsnull);

  // Remove window watcher observer.
  if (mWindowWatcher)
    mWindowWatcher->UnregisterNotification(this);
}


/**
 * Add the window specified by aWindow to the list of windows being watched.
 *
 * \param aWindow               Window to add.
 */

nsresult
sbWindowWatcher::AddWindow(nsIDOMWindow* aWindow)
{
  // Validate arguments and state.
  NS_ASSERTION(aWindow, "aWindow is null");

  // Function variables.
  PRBool success;
  nsresult rv;

  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Create the window info object.
  nsAutoPtr<WindowInfo> windowInfo;
  windowInfo = new WindowInfo();
  NS_ENSURE_TRUE(windowInfo, NS_ERROR_OUT_OF_MEMORY);
  windowInfo->window = aWindow;

  // Get the window event target.
  nsCOMPtr<nsIDOMWindow2> window2 = do_QueryInterface(aWindow, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIDOMEventTarget> windowEventTarget;
  rv = window2->GetWindowRoot(getter_AddRefs(windowEventTarget));
  NS_ENSURE_SUCCESS(rv, rv);
  windowInfo->eventTarget = windowEventTarget;

  // Create a window event listener.
  nsRefPtr<sbWindowWatcherEventListener> eventListener;
  rv = sbWindowWatcherEventListener::New(getter_AddRefs(eventListener),
                                         this,
                                         aWindow);
  NS_ENSURE_SUCCESS(rv, rv);
  windowInfo->eventListener = eventListener;

  // Add the window info to the window info table.
  success = mWindowInfoTable.Put(aWindow, windowInfo.forget());
  NS_ENSURE_TRUE(success, NS_ERROR_OUT_OF_MEMORY);

  // Add the opened window to the window list.
  success = mWindowList.AppendObject(aWindow);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  // Listen for the end of window overlay load events.
  rv = windowEventTarget->AddEventListener(NS_LITERAL_STRING("sb-overlay-load"),
                                           eventListener,
                                           PR_TRUE);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Remove the window specified by aWindow from the list of windows being
 * watched.
 *
 * \param aWindow               Window to remove.
 */

nsresult
sbWindowWatcher::RemoveWindow(nsIDOMWindow* aWindow)
{
  // Validate arguments.
  NS_ASSERTION(aWindow, "aWindow is null");

  // Function variables.
  PRBool success;
  nsresult rv;

  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Get the removed window information.
  WindowInfo* windowInfo;
  success = mWindowInfoTable.Get(aWindow, &windowInfo);
  if (!success)
    windowInfo = nsnull;

  // Remove listener for the end of window overlay load events.
  if (windowInfo) {
    rv = windowInfo->eventTarget->RemoveEventListener
                                    (NS_LITERAL_STRING("sb-overlay-load"),
                                     windowInfo->eventListener,
                                     PR_TRUE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Remove the closed window from the window information map.
  mWindowInfoTable.Remove(aWindow);

  // Remove the closed window from the window list.
  mWindowList.RemoveObject(aWindow);

  return NS_OK;
}


/**
 * Remove all windows from the list of windows being watched.
 *
 * \param aWindow               Window to remove.
 */

void
sbWindowWatcher::RemoveAllWindows()
{
  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Remove all of the windows.
  PRInt32 windowCount = mWindowList.Count();
  for (PRInt32 i = windowCount - 1; i >= 0; i--) {
    RemoveWindow(mWindowList[i]);
  }
}


/**
 * Handle a window ready event for the window specified by aWindow.
 *
 * \param aWindow               Window that has become ready.
 */

void
sbWindowWatcher::OnWindowReady(nsIDOMWindow* aWindow)
{
  // Validate arguments.
  NS_ENSURE_TRUE(aWindow, /* void */);

  // Function variables.
  PRBool   success;

  // Operate within the monitor.
  {
    nsAutoMonitor autoMonitor(mMonitor);

    // Get the window information.  Do nothing if not available.
    WindowInfo* windowInfo;
    success = mWindowInfoTable.Get(aWindow, &windowInfo);
    NS_ENSURE_TRUE(success, /* void */);

    // Indicate that the window is ready.
    windowInfo->isReady = PR_TRUE;
  }

  // Invoke call with window callbacks.
  InvokeCallWithWindowCallbacks(aWindow);
}


/**
 * Return in aWindowType the type of the window specified by aWindow.
 *
 * \param aWindow               Window for which to get type.
 * \param aWindowType           Type of window.
 */

nsresult
sbWindowWatcher::GetWindowType(nsIDOMWindow* aWindow,
                               nsAString&    aWindowType)
{
  // Validate arguments.
  NS_ASSERTION(aWindow, "aWindow is null.");

  // Function variables.
  nsresult rv;

  // Get the window element.  The window won't neccessariy have one.
  nsCOMPtr<nsIDOMElement> element;
  nsCOMPtr<nsIDOMDocument> document;
  rv = aWindow->GetDocument(getter_AddRefs(document));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!document)
    return NS_ERROR_NOT_AVAILABLE;
  rv = document->GetDocumentElement(getter_AddRefs(element));
  NS_ENSURE_SUCCESS(rv, rv);
  if (!element)
    return NS_ERROR_NOT_AVAILABLE;

  // Get the window type attribute.
  rv = element->GetAttribute(NS_LITERAL_STRING("windowtype"), aWindowType);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * Invoke call with window callbacks for the window specified by aWindow.  If
 * aWindow is null, call all call with window callbacks.
 *
 * \param aWindow               Window for which to call callbacks.
 */

nsresult
sbWindowWatcher::InvokeCallWithWindowCallbacks(nsIDOMWindow* aWindow)
{
  // Function variables.
  nsresult rv;

  // Get the window type.
  nsAutoString windowType;
  if (aWindow) {
    rv = GetWindowType(aWindow, windowType);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Operate within the monitor.
  nsAutoMonitor autoMonitor(mMonitor);

  // Call all of the matching callbacks.
  for (PRUint32 i = 0; i < mCallWithWindowList.Length();) {
    // Get the call with window info.
    CallWithWindowInfo& callWithWindowInfo = mCallWithWindowList[i];

    // If the info matches, call callback and remove info from list.  Otherwise,
    // advance to the next info item in list.
    if (!aWindow || windowType.Equals(callWithWindowInfo.windowType)) {
      callWithWindowInfo.callback->HandleWindowCallback(aWindow);
      mCallWithWindowList.RemoveElementAt(i);
    } else {
      i++;
    }
  }

  return NS_OK;
}


/**
 * Return this instance proxied to the main thread in aWindowWatcher.
 *
 * \param aWindowWatcher        Returned proxied window watcher.
 */

nsresult
sbWindowWatcher::GetProxiedWindowWatcher(sbIWindowWatcher** aWindowWatcher)
{
  // Validate arguments.
  NS_ASSERTION(aWindowWatcher, "aWindowWatcher is null");

  // Function variables.
  nsresult rv;

  // Create a main thread proxy for the window watcher.
  nsCOMPtr<nsIProxyObjectManager>
    proxyObjectManager = do_GetService("@mozilla.org/xpcomproxy;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = proxyObjectManager->GetProxyForObject
                             (NS_PROXY_TO_MAIN_THREAD,
                              NS_GET_IID(sbIWindowWatcher),
                              NS_ISUPPORTS_CAST(sbIWindowWatcher *, this),
                                nsIProxyObjectManager::INVOKE_SYNC |
                                nsIProxyObjectManager::FORCE_PROXY_CREATION,
                              (void**) aWindowWatcher);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//
// Songbird window watcher event listener class.
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//
// Songbird window watcher event listener nsISupports implementation.
//
//------------------------------------------------------------------------------

NS_IMPL_THREADSAFE_ISUPPORTS1(sbWindowWatcherEventListener,
                              nsIDOMEventListener)


//------------------------------------------------------------------------------
//
// Songbird window watcher event listener nsIDOMEventListener implementation.
//
//------------------------------------------------------------------------------

/**
 * This method is called whenever an event occurs of the type for which 
 * the EventListener interface was registered.
 *
 * @param   evt The Event contains contextual information about the 
 *              event. It also contains the stopPropagation and 
 *              preventDefault methods which are used in determining the 
 *              event's flow and default action.
 */

NS_IMETHODIMP
sbWindowWatcherEventListener::HandleEvent(nsIDOMEvent* event)
{
  // Validate arguments.
  NS_ENSURE_ARG_POINTER(event);

  // Function variables.
  nsresult rv;

  // Ensure the window watcher still exists.
  nsCOMPtr<sbIWindowWatcher>
    windowWatcher = do_QueryReferent(mWeakSBWindowWatcher, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the event type.
  nsAutoString eventType;
  rv = event->GetType(eventType);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dispatch processing of event.
  if (eventType.LowerCaseEqualsLiteral("sb-overlay-load"))
    mSBWindowWatcher->OnWindowReady(mWindow);

  return NS_OK;
}


//------------------------------------------------------------------------------
//
// Songbird window watcher event listener implementation.
//
//------------------------------------------------------------------------------

/**
 * Create a new instance of a Songbird window watcher event listener and return
 * it in aListener.  The Songbird window watcher object is specified by
 * aSBWindowWatcher and the window for which events are being listened is
 * specified by aWindow.
 *
 * \param aListener             Songbird window watcher event listener .
 * \param aSBWindowWatcher      Songbird window watcher object.
 * \param aWindow               Window for which to listen for events.
 */

/* static */
nsresult
sbWindowWatcherEventListener::New
  (sbWindowWatcherEventListener** aListener,
   sbWindowWatcher*               aSBWindowWatcher,
   nsIDOMWindow*                  aWindow)
{
  // Validate arguments.
  NS_ENSURE_ARG_POINTER(aListener);

  // Function variables.
  nsresult rv;

  // Create the listener object.
  nsRefPtr<sbWindowWatcherEventListener> listener;
  listener = new sbWindowWatcherEventListener(aSBWindowWatcher, aWindow);
  NS_ENSURE_TRUE(listener, NS_ERROR_OUT_OF_MEMORY);

  // Initialize the listener object.
  rv = listener->Initialize();
  NS_ENSURE_SUCCESS(rv, rv);

  // Return results.
  NS_ADDREF(*aListener = listener);

  return NS_OK;
}


//------------------------------------------------------------------------------
//
// Internal Songbird window watcher event listener implementation.
//
//------------------------------------------------------------------------------

/**
 * Initialize the Songbird window watcher event listener.
 */

nsresult
sbWindowWatcherEventListener::Initialize()
{
  nsresult rv;

  // Get a weak reference to the window watcher object.
  nsCOMPtr<nsISupportsWeakReference>
    weakSBWindowWatcher =
      do_QueryInterface(NS_ISUPPORTS_CAST(sbIWindowWatcher*, mSBWindowWatcher),
                        &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = weakSBWindowWatcher->GetWeakReference
                              (getter_AddRefs(mWeakSBWindowWatcher));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//
// Songbird window watcher wait for window class.
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//
// Songbird window watcher wait for window nsISupports implementation.
//
//------------------------------------------------------------------------------

NS_IMPL_THREADSAFE_ISUPPORTS1(sbWindowWatcherWaitForWindow,
                              sbICallWithWindowCallback)


//------------------------------------------------------------------------------
//
// Songbird window watcher wait for window sbICallWithWindowCallback
// implementation.
//
//------------------------------------------------------------------------------

/**
 * \brief Handle the callback with the window specified by aWindow.
 *
 * \param aWindow             Callback window.
 */

NS_IMETHODIMP
sbWindowWatcherWaitForWindow::HandleWindowCallback(nsIDOMWindow* aWindow)
{
  // Operate under the ready monitor.
  nsAutoMonitor autoReadyMonitor(mReadyMonitor);

  // Get the ready window.
  mWindow = aWindow;
  mReady = PR_TRUE;

  // Send notification that the window is ready.
  autoReadyMonitor.Notify();

  return NS_OK;
}


//------------------------------------------------------------------------------
//
// Songbird window watcher wait for window implementation.
//
//------------------------------------------------------------------------------

/**
 * Create a new instance of a wait for window object and return it in
 * aWaitForWindow.
 *
 * \param aWaitForWindow        Wait for window object.
 */

/* static */
nsresult
sbWindowWatcherWaitForWindow::New(sbWindowWatcherWaitForWindow** aWaitForWindow)
{
  // Validate arguments.
  NS_ENSURE_ARG_POINTER(aWaitForWindow);

  // Function variables.
  nsresult rv;

  // Create the wait for window object.
  nsRefPtr<sbWindowWatcherWaitForWindow> waitForWindow;
  waitForWindow = new sbWindowWatcherWaitForWindow();
  NS_ENSURE_TRUE(waitForWindow, NS_ERROR_OUT_OF_MEMORY);

  // Initialize the wait for window object.
  rv = waitForWindow->Initialize();
  NS_ENSURE_SUCCESS(rv, rv);

  // Return results.
  NS_ADDREF(*aWaitForWindow = waitForWindow);

  return NS_OK;
}


/**
 * Destroy the wait for window object.
 */

sbWindowWatcherWaitForWindow::~sbWindowWatcherWaitForWindow()
{
  // Dispose of the monitor.
  if (mReadyMonitor)
    nsAutoMonitor::DestroyMonitor(mReadyMonitor);
  mReadyMonitor = nsnull;

  // Remove object references.
  mSBWindowWatcher = nsnull;
  mWindow = nsnull;
}


/**
 * \brief Wait until a window of the type specified by aWindowType is
 *        available or until shutdown.
 *
 * \param aWindowType         Type of window to get.
 */

NS_IMETHODIMP
sbWindowWatcherWaitForWindow::Wait(const nsAString& aWindowType)
{
  nsresult rv;

  // Set up to call this instance with a matching window.
  rv = mSBWindowWatcher->CallWithWindow(aWindowType, this);
  NS_ENSURE_SUCCESS(rv, rv);

  // Operate under the ready monitor.
  nsAutoMonitor autoReadyMonitor(mReadyMonitor);

  // Wait for a window to be ready.
  if (!mReady) {
    rv = autoReadyMonitor.Wait();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}


//------------------------------------------------------------------------------
//
// Internal Songbird window watcher wait for window implementation.
//
//------------------------------------------------------------------------------

/**
 * Construct a wait for window object.
 */

sbWindowWatcherWaitForWindow::sbWindowWatcherWaitForWindow() :
  mReadyMonitor(nsnull),
  mReady(PR_FALSE)
{
}


/**
 * Initialize the wait for window object.
 */

nsresult
sbWindowWatcherWaitForWindow::Initialize()
{
  nsresult rv;

  // Get the Songbird window watcher service.
  mSBWindowWatcher =
    do_GetService("@songbirdnest.com/Songbird/window-watcher;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create a monitor.
  mReadyMonitor =
    nsAutoMonitor::NewMonitor("sbWindowWatcherWaitForWindow::mReadyMonitor");
  NS_ENSURE_TRUE(mReadyMonitor, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

