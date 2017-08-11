#include <stdio.h>
#include <tchar.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <Wbemidl.h>
#include <atlbase.h>

#pragma comment(lib, "wbemuuid.lib")

typedef struct _volume_desc_t {
	std::wstring id;
	std::wstring letter;
	std::wstring physicalDriveStr;
	int physicalDriveNum;
} volume_desc_t;

void PrintUsage( void )
{
	wprintf( L"MakeUSB \n"
			 L" Usage : makeusb [options] [image] [device] \n"
			 L" Options : \n"
			 L" -w   Write image directly to USB device \n"
			 L" -v   Print version number \n"
			 L" -h   Print help info \n" );
}

BOOL ScanRemovableVolumes( IWbemServices* pServices, std::vector<volume_desc_t>& volumes )
{
	HRESULT hr;
	CIMTYPE cType;
	BOOL retval = FALSE;
	CComPtr<IEnumWbemClassObject> pEnumerator;

	CComBSTR bstrWql( L"WQL" );
	CComBSTR bstrQuery( L"SELECT * FROM Win32_Volume WHERE DriveType=2" );

	hr = pServices->ExecQuery( bstrWql, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, (IEnumWbemClassObject**)&pEnumerator );
	if (SUCCEEDED( hr ))
	{
		ULONG uReturn;

		VOLUME_DISK_EXTENTS diskExtents;
		wchar_t wszVolNum[24];
		std::wstring wstrPhysicalDrive;

		BOOL ret = FALSE;
		HANDLE hvol = INVALID_HANDLE_VALUE;
		DWORD dwBytesNeeded = 0;

		volume_desc_t volumeDesc;

		while (pEnumerator)
		{
			CComPtr<IWbemClassObject> pclsObj;
			CComVariant vDeviceID;
			CComVariant vDriveLetter;

			hvol = INVALID_HANDLE_VALUE;

			hr = pEnumerator->Next( WBEM_INFINITE, 1, &pclsObj, &uReturn );
			if (FAILED( hr ))
			{
				wprintf( L"Failed to enumerate volumes. Error %X \n", hr );
				goto fail;
			}

			// We are done
			if (uReturn == 0)
				break;

			// Get the DeviceID property
			hr = pclsObj->Get( L"DeviceID", 0, &vDeviceID, &cType, 0 );
			if (FAILED( hr ))
			{
				wprintf( L"Failed to get DeviceID property for enumerable volume. Error %d \n", hr );
				goto fail;
			}

			if (vDeviceID.vt == VT_BSTR)
			{
				volumeDesc.id = vDeviceID.bstrVal;
				volumeDesc.id.erase( volumeDesc.id.size( ) - 1 ); // Remove trailing backslash
			}
			else
			{
				wprintf( L"Error: WMI DeviceID is not a BSTR (%d) \n", E_UNEXPECTED );
				goto fail;
			}

			hr = pclsObj->Get( L"DriveLetter", 0, &vDriveLetter, &cType, 0 );
			if (FAILED( hr ))
			{
				wprintf( L"Failed to get DriveLetter property for enumerable volume. Error %d \n", hr );
				goto fail;
			}

			if (vDriveLetter.vt == VT_BSTR)
			{
				volumeDesc.letter = vDriveLetter.bstrVal;
			}
			else
			{
				if (vDriveLetter.vt == VT_NULL)
				{
					wprintf( L"WARNING: Drive %s is not assigned a drive letter! \n", volumeDesc.id.c_str( ) );
				}
				else
				{
					wprintf( L"Error: WMI DriveLetter type is invalid (%d) \n", vDriveLetter.vt );
					goto fail;
				}
			}

			hvol = CreateFileW(
				volumeDesc.id.c_str( ),
				GENERIC_WRITE,
				FILE_SHARE_WRITE, 
				NULL,
				OPEN_EXISTING,
				0,
				NULL
			);
			if (hvol == INVALID_HANDLE_VALUE)
			{
				wprintf( L"CreateFile failed to open Volume %s - Error = 0x%X \n", volumeDesc.id.c_str( ), GetLastError( ) );
				goto fail;
			}

			ret = DeviceIoControl( hvol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &diskExtents, sizeof( VOLUME_DISK_EXTENTS ), &dwBytesNeeded, NULL );
			if (!ret)
			{
				wprintf( L"Failed to get Volume disk extents %s \n", volumeDesc.id.c_str( ) );
				goto fail;
			}

			if (diskExtents.NumberOfDiskExtents != 1) // Fail if volume has more than one extent on USB
			{
				wprintf( L"Volume can only have 1 disk extent %s \n", volumeDesc.id.c_str( ) );
				goto fail;
			}

			bool duplicate = false;
			for (auto curVolume : volumes)
			{
				if (curVolume.physicalDriveNum == diskExtents.Extents[0].DiskNumber)
				{
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;

			wprintf( L"DiskNumber = %u\n", diskExtents.Extents[0].DiskNumber );

			_ultow_s( diskExtents.Extents[0].DiskNumber, wszVolNum, _ARRAYSIZE( wszVolNum ), 10 );
			wstrPhysicalDrive.assign( L"\\\\.\\PhysicalDrive" );
			wstrPhysicalDrive += wszVolNum;

			volumeDesc.physicalDriveStr = wstrPhysicalDrive;
			volumeDesc.physicalDriveNum = diskExtents.Extents[0].DiskNumber;

			wprintf( L"  %s -> %s => %s => %s \n", wszVolNum, volumeDesc.letter.c_str( ), volumeDesc.physicalDriveStr.c_str( ), volumeDesc.id.c_str( ) );

			CloseHandle( hvol );

			volumes.push_back( volumeDesc );
		}

		retval = TRUE;
		goto done;

	fail:
		retval = FALSE;
		if (hvol != INVALID_HANDLE_VALUE)
			CloseHandle( hvol );
	}
	else
	{
		wprintf( L"ExecQuery failed with error %X \n", hr );
	}

done:
	return retval;
}

int wmain( int argc, wchar_t *argv[] )
{
	int rc = 0;
	HRESULT hr;
	BOOL ret;
	HANDLE hvol = INVALID_HANDLE_VALUE;
	HANDLE hsrc = INVALID_HANDLE_VALUE;
	HANDLE hdest = INVALID_HANDLE_VALUE;
	const DWORD bufsize = 128 * 1024;
	DWORD dwRead = 0, dwWritten = 0, dwRet = 0;
	ULONGLONG bytesWritten = 0;
	BYTE buf[bufsize] = { 0 };

	wchar_t* src = argv[1];
	std::wstring destVolumeId;
	std::wstring destPhysicalDrive;
	int destNum;

	std::vector<volume_desc_t> vVols;
	CComPtr<IWbemLocator> pLocator;
	CComPtr<IWbemServices> pWbemSvc;

	CComBSTR bstrNamespace( L"ROOT\\CIMV2" );

	if (argc < 2)
	{
		PrintUsage( );
		return -1;
	}

	if (CoInitialize( NULL ) == S_OK)
	{	
		hr = CoCreateInstance( CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&pLocator );
		if (FAILED( hr ))
		{
			wprintf( L"CoCreateInstance failed with error %d \n", hr );
			return hr;
		}

		pLocator->ConnectServer( bstrNamespace, NULL, NULL, NULL, 0, NULL, NULL, (IWbemServices**)&pWbemSvc );
		if (FAILED( hr ))
		{
			wprintf( L"ConnectServer failed to connect to ROOT\\CIMV2 WMI namespace with error %d \n", hr );
			return hr;
		}

		wprintf( L"Connected to ROOT\\CIMV2 WMI namespace \n" );

		hr = CoSetProxyBlanket(
			pWbemSvc,                    // Indicates the proxy to set
			RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
			RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
			NULL,                        // Server principal name
			RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx
			RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
			NULL,                        // client identity
			EOAC_NONE                    // proxy capabilities
		);
		if (FAILED( hr ))
		{
			wprintf( L"CoSetProxyBlanket failed with error %d \n", hr );
			return hr;
		}

		wprintf( L"Choose a destination from the list below: \n" );
		if (!ScanRemovableVolumes( pWbemSvc, vVols ))
		{
			wprintf( L"Failed to scan for removeable devices! \nPress any key to exit..." );
			getchar( );
			return -1;
		}

		if (vVols.empty( ))
		{
			wprintf( L"No removable volumes found, aborting... \nPress any key to exit..." );
			getchar( );
			return -1;
		}

		for (;;)
		{
			bool found = false;
			wprintf( L"=> " );
			scanf_s( "%d", &destNum );
				
			for (auto& vol : vVols)
			{
				if (vol.physicalDriveNum == destNum)
				{
					found = true;
					destPhysicalDrive = vol.physicalDriveStr;
					destVolumeId = vol.id;
					break;
				}
			}

			if (found)
			{
				break;
			}
			else
			{
				wprintf( L"Not a valid destination! \n" );
			}
		}

		
		hvol = CreateFileW(
			destVolumeId.c_str( ),
			GENERIC_WRITE,
			FILE_SHARE_WRITE, 
			NULL,
			OPEN_EXISTING,
			0,
			NULL
		);

		if (hvol == INVALID_HANDLE_VALUE)
		{
			rc++;
			wprintf( L"CreateFile failed to open Volume %s - Error = 0x%X \n", destVolumeId.c_str( ), GetLastError( ) );
			goto finished;
		}

		ret = DeviceIoControl( hvol, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dwRet, NULL );
		if (!ret)
		{
			rc++;
			wprintf( L"Failed to lock Volume %s \n", destVolumeId.c_str( ) );
			goto finished;
		}

		ret = DeviceIoControl( hvol, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwRet, NULL );
		if (!ret)
		{
			rc++;
			wprintf( L"Failed to dismount Volume %s \n", destVolumeId.c_str( ) );
			goto finished;
		}

		hsrc = CreateFileW(
			src,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);	
		if (hsrc == INVALID_HANDLE_VALUE)
		{
			rc++;
			wprintf( L"CreateFile failed to open source %s \n", src );
			goto finished;
		}
		
		hdest = CreateFileW(
			destPhysicalDrive.c_str( ),
			GENERIC_WRITE,
			FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING, 
			FILE_ATTRIBUTE_DEVICE,
			NULL
		);
		if (hdest == INVALID_HANDLE_VALUE)
		{
			rc++;
			wprintf( L"CreateFile failed to open target destination %s - ERROR 0x%08X \n", destPhysicalDrive.c_str( ), GetLastError( ) );
			goto finished;
		}

		wprintf( L"Starting raw copy of image %s to %s \n", src, destPhysicalDrive.c_str( ) );
		
		for (;;)
		{
			ret = ReadFile( hsrc, buf, bufsize, &dwRead, NULL );
			if (!ret)
			{
				wprintf( L"ReadFile failed to read source %s \n", src );
				break;
			}

			if (dwRead == 0)
				break;

			ret = WriteFile( hdest, buf, dwRead, &dwWritten, NULL );
			if (!ret)
			{
				wprintf( L"WriteFile failed to write to destination %s \n", destPhysicalDrive.c_str( ) );
				break;
			}

			if (dwRead != dwWritten)
			{
				ret = FALSE;
				wprintf( L"Write to destination failed! dwRead != dwWritten \n" );
				break;
			}

			bytesWritten += dwWritten;
			if (dwRead != bufsize)
				break;
		}
		
		if (ret != FALSE)
		{
			wprintf( L"MakeUSB has copied %lld bytes from %s to %s \n", bytesWritten, src, destPhysicalDrive.c_str( ) );

			ret = DeviceIoControl( hvol, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &dwRet, NULL );
			if (!ret)
			{
				rc++;
				wprintf( L"Failed to unlock volume %s \n", destVolumeId.c_str( ) );
			}

		finished:
			if (hvol != INVALID_HANDLE_VALUE)
				CloseHandle( hvol );
			if (hsrc != INVALID_HANDLE_VALUE)
				CloseHandle( hsrc );
			if (hdest != INVALID_HANDLE_VALUE)
				CloseHandle( hdest );
		}
	}

	wprintf( L"Press any key to exit..." );
	getchar( );
	getchar( );

	return rc;
}

