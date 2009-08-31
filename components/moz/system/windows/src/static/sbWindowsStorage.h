/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2009 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */

#ifndef __SB_WINDOWS_STORAGE_H__
#define __SB_WINDOWS_STORAGE_H__

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//
// Songbird Windows storage device services defs.
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

/**
 * \file  sbWindowsStorage.h
 * \brief Songbird Windows Storage Device Services Definitions.
 */

//------------------------------------------------------------------------------
//
// Songbird Windows storage device imported services.
//
//------------------------------------------------------------------------------

// Windows imports.
#include <devioctl.h>
#include <objbase.h>

#include <ntddstor.h>


//------------------------------------------------------------------------------
//
// Songbird Windows storage device services.
//
//------------------------------------------------------------------------------

/**
 * Return in aStorageDevNum the storage device number for the device with the
 * file path specified by aDevPath.
 *
 * \param aDevPath              Device file path.
 * \param aStorageDevNum        Returned storage device number.
 */
HRESULT sbWinGetStorageDevNum(LPWSTR                 aDevPath,
                              STORAGE_DEVICE_NUMBER* aStorageDevNum);


#endif // __SB_WINDOWS_STORAGE_H__

