// https://docs.microsoft.com/en-us/windows/win32/DevIO/calling-deviceiocontrol

#define UNICODE 1
#define _UNICODE 1

/* The code of interest is in the subroutine GetDriveGeometry. The
   code in main shows how to interpret the results of the call. */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <conio.h>
#include <atlstr.h>

#define ToString(value) L##value
#define wszDrive L"\\\\.\\PhysicalDrive0"

BOOL GetDriveGeometry(LPTSTR wszPath, DISK_GEOMETRY* pdg)
{
    BOOL bResult = FALSE;                 // results flag
    DWORD junk = 0;                     // discard results

    HANDLE hDevice = CreateFile(wszPath,          // drive to open
        0,                // no access to the drive
        FILE_SHARE_READ | // share mode
        FILE_SHARE_WRITE,
        NULL,             // default security attributes
        OPEN_EXISTING,    // disposition
        0,                // file attributes
        NULL);            // do not copy file attributes

    if (hDevice == INVALID_HANDLE_VALUE)    // cannot open the drive
    {
        return (FALSE);
    }

    bResult = DeviceIoControl(hDevice,                       // device to be queried
        IOCTL_DISK_GET_DRIVE_GEOMETRY, // operation to perform
        NULL, 0,                       // no input buffer
        pdg, sizeof(*pdg),            // output buffer
        &junk,                         // # bytes returned
        (LPOVERLAPPED)NULL);          // synchronous I/O

    CloseHandle(hDevice);

    return (bResult);
}

// http://codexpert.ro/blog/2013/10/26/get-physical-drive-serial-number-part-1/
DWORD GetPhysicalDriveSerialNumber(UINT nDriveNumber IN, CString& strSerialNumber OUT)
{
    DWORD dwRet = NO_ERROR;
    strSerialNumber.Empty();

    // Format physical drive path (may be '\\.\PhysicalDrive0', '\\.\PhysicalDrive1' and so on).
    CString strDrivePath;
    strDrivePath.Format(_T("\\\\.\\PhysicalDrive%u"), nDriveNumber);

    // Get a handle to physical drive
    HANDLE hDevice = ::CreateFile(strDrivePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (INVALID_HANDLE_VALUE == hDevice)
        return ::GetLastError();

    // Set the input data structure
    STORAGE_PROPERTY_QUERY storagePropertyQuery;
    ZeroMemory(&storagePropertyQuery, sizeof(STORAGE_PROPERTY_QUERY));
    storagePropertyQuery.PropertyId = StorageDeviceProperty;
    storagePropertyQuery.QueryType = PropertyStandardQuery;

    // Get the necessary output buffer size
    STORAGE_DESCRIPTOR_HEADER storageDescriptorHeader = { 0 };
    DWORD dwBytesReturned = 0;
    if (!::DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
        &storagePropertyQuery, sizeof(STORAGE_PROPERTY_QUERY),
        &storageDescriptorHeader, sizeof(STORAGE_DESCRIPTOR_HEADER),
        &dwBytesReturned, NULL))
    {
        dwRet = ::GetLastError();
        ::CloseHandle(hDevice);
        return dwRet;
    }

    // Alloc the output buffer
    const DWORD dwOutBufferSize = storageDescriptorHeader.Size;
    BYTE* pOutBuffer = new BYTE[dwOutBufferSize];
    ZeroMemory(pOutBuffer, dwOutBufferSize);

    // Get the storage device descriptor
    if (!::DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
        &storagePropertyQuery, sizeof(STORAGE_PROPERTY_QUERY),
        pOutBuffer, dwOutBufferSize,
        &dwBytesReturned, NULL))
    {
        dwRet = ::GetLastError();
        delete[]pOutBuffer;
        ::CloseHandle(hDevice);
        return dwRet;
    }

    // Now, the output buffer points to a STORAGE_DEVICE_DESCRIPTOR structure
    // followed by additional info like vendor ID, product ID, serial number, and so on.
    STORAGE_DEVICE_DESCRIPTOR* pDeviceDescriptor = (STORAGE_DEVICE_DESCRIPTOR*)pOutBuffer;
    const DWORD dwSerialNumberOffset = pDeviceDescriptor->SerialNumberOffset;
    if (dwSerialNumberOffset != 0)
    {
        // Finally, get the serial number
        strSerialNumber = CString(pOutBuffer + dwSerialNumberOffset);
    }

    // Do cleanup and return
    delete[]pOutBuffer;

    static STORAGE_PROPERTY_QUERY spq = { StorageDeviceProperty, PropertyStandardQuery };

    union {
        PVOID buf;
        PSTR psz;
        PSTORAGE_DEVICE_DESCRIPTOR psdd;
    };

    ULONG size = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 0x100;

    ULONG dwError;

    do
    {
        dwError = ERROR_NO_SYSTEM_RESOURCES;

        if (buf = LocalAlloc(0, size))
        {
            ULONG BytesReturned;

            if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &spq, sizeof(spq), buf, size, &BytesReturned, 0))
            {
                if (psdd->Version >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
                {
                    if (psdd->Size > size)
                    {
                        size = psdd->Size;
                        dwError = ERROR_MORE_DATA;
                    }
                    else
                    {
                        if (psdd->SerialNumberOffset)
                        {
                            printf("\nSerialNumber via 2nd Method = %s\t \nType=%d\n\n", psz + psdd->SerialNumberOffset, psdd->BusType);
                            dwError = NOERROR;
                        }
                        else
                        {
                            dwError = ERROR_NO_DATA;
                        }
                    }
                }
                else
                {
                    dwError = ERROR_GEN_FAILURE;
                }
            }
            else
            {
                dwError = GetLastError();
            }

            LocalFree(buf);
        }
    } while (dwError == ERROR_MORE_DATA);

    ::CloseHandle(hDevice);
    return dwRet;
}

int wmain(int argc, wchar_t* argv[])
{
    DISK_GEOMETRY pdg = { 0 }; // disk drive geometry structure
    ULONGLONG DiskSize = 0;    // size of the drive, in bytes
    BOOL bResult = FALSE;
    bResult = GetDriveGeometry((LPTSTR)wszDrive, &pdg);
    for (int i = 0; i < 2; i++)
    {
        CString strSerialNumber;
        bResult = GetPhysicalDriveSerialNumber(i, strSerialNumber);

        if (!strSerialNumber.IsEmpty())
        {
            wprintf(L"%s\n", strSerialNumber);
        }
    }
    if (bResult)
    {
        wprintf(L"Drive path      = %ws\n", wszDrive);
        wprintf(L"Cylinders       = %I64d\n", pdg.Cylinders);
        wprintf(L"Tracks/cylinder = %ld\n", (ULONG)pdg.TracksPerCylinder);
        wprintf(L"Sectors/track   = %ld\n", (ULONG)pdg.SectorsPerTrack);
        wprintf(L"Bytes/sector    = %ld\n", (ULONG)pdg.BytesPerSector);

        DiskSize = pdg.Cylinders.QuadPart * (ULONG)pdg.TracksPerCylinder *
            (ULONG)pdg.SectorsPerTrack * (ULONG)pdg.BytesPerSector;
        wprintf(L"Disk size       = %I64d (Bytes)\n"
            L"                = %.2f (Gb)\n",
            DiskSize, (double)DiskSize / (1024 * 1024 * 1024));
    }
    else
    {
        wprintf(L"GetDriveGeometry failed. Error %ld.\n", GetLastError());
    }

    _getche();

    return ((int)bResult);
}