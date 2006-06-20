/*
//
// BEGIN SONGBIRD GPL
// 
// This file is part of the Songbird web player.
//
// Copyright� 2006 Pioneers of the Inevitable LLC
// http://songbirdnest.com
// 
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the �GPL�).
// 
// Software distributed under the License is distributed 
// on an �AS IS� basis, WITHOUT WARRANTY OF ANY KIND, either 
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

/** 
* \file  WindowResizeHook.h
* \brief Service for setting min/max limit callbacks to a window position and size - Prototypes.
*/

#ifndef __WINDOW_RESIZE_HOOK_H__
#define __WINDOW_RESIZE_HOOK_H__

// INCLUDES ===================================================================

// XXX Remove Me !!!
#ifndef PRUSTRING_DEFINED
#define PRUSTRING_DEFINED
#include <string>
#include "nscore.h"
namespace std
{
  typedef basic_string< PRUnichar > prustring;
};
#endif

#include "IWindowResizeHook.h"
#include "NativeWindowFromNode.h"
#include <list>

// DEFINES ====================================================================
#define SONGBIRD_WINDOWRESIZEHOOK_CONTRACTID  "@songbird.org/Songbird/WindowResizeHook;1"
#define SONGBIRD_WINDOWRESIZEHOOK_CLASSNAME   "Songbird Window Resize Hook Interface"

// {D44A410E-70A1-4932-8F5A-B1D644356401}
#define SONGBIRD_WINDOWRESIZEHOOK_CID { 0xd44a410e, 0x70a1, 0x4932, { 0x8f, 0x5a, 0xb1, 0xd6, 0x44, 0x35, 0x64, 0x1 } }

// CLASSES ====================================================================

class CWindowResizeHookItem
{
public:
  NATIVEWINDOW m_window;
  sbIWindowResizeHookCallback *m_callback;
};

LRESULT CALLBACK ResizeHook(int code, WPARAM wParam, LPARAM lParam);

class CWindowResizeHook : public sbIWindowResizeHook
{
public:
  CWindowResizeHook();
  virtual ~CWindowResizeHook();

  NS_DECL_ISUPPORTS
  NS_DECL_SBIWINDOWRESIZEHOOK
  
public:
#ifdef WIN32
  static HHOOK m_hookid;
#endif
  static CWindowResizeHookItem *findItemByWindow(NATIVEWINDOW wnd);

protected:
  static CWindowResizeHookItem *findItemByCallback(sbIWindowResizeHookCallback *cb);
  static std::list<CWindowResizeHookItem *> m_items;
};

#endif // __WINDOW_RESIZE_HOOK_H__

