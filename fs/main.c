/*

SNEEK - SD-NAND/ES emulation kit for Nintendo Wii

Copyright (C) 2009-2010  crediar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#include "string.h"
#include "syscalls.h"
#include "global.h"
#include "ipc.h"
#include "diskio.h"
#include "ff.h"
#include "sdhcvar.h"
#include "FS.h"

FATFS fatfs;
int verbose = 0;
u32 base_offset=0;
static char Heap[0x100] ALIGNED(32);
void *QueueSpace = NULL;
int QueueID = 0;
int HeapID=0;

#undef DEBUG

s32 RegisterDevices( void )
{
	QueueID = mqueue_create(QueueSpace, 8);

//	s32 ret = device_register("/dev/flash", QueueID);
//#ifdef DEBUG
//	dbgprintf("FFS:DeviceRegister(\"/dev/flash\"):%d\n", ret );
//#endif
//	if( ret < 0 )
//		return ret;
//
//	ret = device_register("/dev/boot2", QueueID);
//#ifdef DEBUG
//	dbgprintf("FFS:DeviceRegister(\"/dev/boot2\"):%d\n", ret );
//#endif
//	if( ret < 0 )
//		return ret;

	s32 ret = device_register("/", QueueID);
#ifdef DEBUG
	dbgprintf("FFS:DeviceRegister(\"/\"):%d\n", ret );
#endif
	if( ret < 0 )
		return ret;

//	ret = device_register("/dev/sdio", QueueID );
//#ifdef DEBUG
//	dbgprintf("FFS:DeviceRegister(\"/dev/sdio\"):%d QueueID:%d\n", ret, QueueID );
//#endif

	return QueueID;
}
int _main( int argc, char *argv[] )
{
	thread_set_priority( 0, 0x58 );

#ifdef DEBUG
	dbgprintf("$IOSVersion: FFS-SD: %s %s 64M DEBUG$\n", __DATE__, __TIME__ );
#else
	dbgprintf("$IOSVersion: FFS-SD: %s %s 64M Release$\n", __DATE__, __TIME__ );
#endif


	int fres=0;
	s32 ret=0;
	struct IPCMessage *CMessage=NULL;

	//dbgprintf("FFS:Heap Init...");
	HeapID = heap_create(Heap, sizeof Heap);
	QueueSpace = heap_alloc(HeapID, 0x20);
	//dbgprintf("ok\n");

	QueueID = RegisterDevices();
	if( QueueID < 0 )
	{
		ThreadCancel( 0, 0x77 );
	}

	sdhc_init();

	//dbgprintf("FFS:Mounting SD...\n");
	fres = f_mount(0, &fatfs);
	//dbgprintf("FFS:f_mount():%d\n", fres);

	if(fres != FR_OK)
	{
		dbgprintf("FFS:Error %d while trying to mount SD\n", fres);
		ThreadCancel( 0, 0x77 );
	}

	//dbgprintf("FFS:Clean up...");

	//clean up folders
	FS_Delete("/tmp");
	FS_Delete("/import");

	f_mkdir("/tmp");
	f_mkdir("/import");

	//dbgprintf("ok\n");

	while (1)
	{
		ret = mqueue_recv(QueueID, (void *)&CMessage, 0);
		if( ret != 0 )
		{
			//dbgprintf("FFS:mqueue_recv(%d) FAILED:%d\n", QueueID, ret);
			continue;
		}

		switch (CMessage->command)
		{
			case IOS_OPEN:
			{
				ret = FS_Open( CMessage->open.device, CMessage->open.mode );
#ifdef DEBUG
				if( ret != FS_ENOENT )
					dbgprintf("FFS:IOS_Open(\"%s\", %d):%d\n", CMessage->open.device, CMessage->open.mode, ret );
#endif
				mqueue_ack( (void *)CMessage, ret);
				
			} break;
			
			case IOS_CLOSE:
			{
				ret = FS_Close( CMessage->fd );
#ifdef DEBUG
				dbgprintf("FFS:IOS_Close(%d):%d\n", CMessage->fd, ret );
#endif
				mqueue_ack( (void *)CMessage, ret);
			} break;

			case IOS_READ:
			{

				ret = FS_Read( CMessage->fd, CMessage->read.data, CMessage->read.length );
					
#ifdef DEBUG
				dbgprintf("FFS:IOS_Read(%d, 0x%p, %d):%d\n", CMessage->fd, CMessage->read.data, CMessage->read.length, ret );
#endif
				mqueue_ack( (void *)CMessage, ret );

			} break;
			case IOS_WRITE:
			{
				if( (vu32)(CMessage->write.data)>>28 )
				{
					ret = FS_Write( CMessage->fd, CMessage->write.data, CMessage->write.length );

				} else {

					u8 *buf = heap_alloc_aligned( 0, CMessage->write.length, 0x40 );
					memcpy( buf, CMessage->write.data, CMessage->write.length );

					ret = FS_Write( CMessage->fd, CMessage->write.data, CMessage->write.length );

					heap_free( 0, buf );

				}
#ifdef DEBUG
				dbgprintf("FFS:IOS_Write(%d, 0x%p, %d):%d\n", CMessage->fd, CMessage->write.data, CMessage->write.length, ret );
#endif
				mqueue_ack( (void *)CMessage, ret );
			} break;
			case IOS_SEEK:
			{
				ret = FS_Seek( CMessage->fd, CMessage->seek.offset, CMessage->seek.origin );

#ifdef DEBUG
				dbgprintf("FFS:IOS_Seek(%d, %d, %d):%d\n", CMessage->fd, CMessage->seek.offset, CMessage->seek.origin, ret);
#endif
				mqueue_ack( (void *)CMessage, ret );

			} break;
			
			case IOS_IOCTL:
				if( CMessage->fd == SD_FD )
				{
					if( CMessage->ioctl.command == 0x0B )
						*(vu32*)(CMessage->ioctl.buffer_io) = 2;

					mqueue_ack( (void *)CMessage, 0);
				} else
					FFS_Ioctl(CMessage);
			break;	

			case IOS_IOCTLV:
				if( CMessage->fd == FL_FD )
				{
					//dbgprintf("FFS:IOS_Ioctlv( %d 0x%x %d %d 0x%p )\n", CMessage->fd, CMessage->ioctlv.command, CMessage->ioctlv.argc_in, CMessage->ioctlv.argc_io, CMessage->ioctlv.argv);
					mqueue_ack( (void *)CMessage, -1017);
				} else
					FFS_Ioctlv(CMessage);
			break;
#ifdef EDEBUG
			default:
				dbgprintf("FFS:unimplemented/invalid msg: %08x\n", CMessage->command);
				mqueue_ack( (void *)CMessage, -1017);
#endif
		}
	}

	return 0;
}
