.386
.model flat
EXTERN _YuanCompatibleSystemTime@4:PROC
EXTERN _YuanCompatibleCreateFile2@20:PROC
EXTERN _YuanCompatibleCopyFile2@12:PROC
.data
PUBLIC __imp__GetSystemTimePreciseAsFileTime@4
__imp__GetSystemTimePreciseAsFileTime@4 DWORD OFFSET _YuanCompatibleSystemTime@4
PUBLIC __imp__CreateFile2@20
__imp__CreateFile2@20 DWORD OFFSET _YuanCompatibleCreateFile2@20
PUBLIC __imp__CopyFile2@12
__imp__CopyFile2@12 DWORD OFFSET _YuanCompatibleCopyFile2@12
END
