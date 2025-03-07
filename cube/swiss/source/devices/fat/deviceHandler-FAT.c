/* deviceHandler-FAT.c
	- device implementation for FAT (via SD Card Adapters/IDE-EXI)
	by emu_kidid
 */

#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ogc/dvd.h>
#include <ogc/machine/processor.h>
#include <sdcard/gcsd.h>
#include "deviceHandler.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "ata.h"
#include "patcher.h"
#include "dvd.h"

const DISC_INTERFACE* carda = &__io_gcsda;
const DISC_INTERFACE* cardb = &__io_gcsdb;
const DISC_INTERFACE* cardc = &__io_gcsd2;
FATFS *fs[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
#define SD_COUNT 3
#define IS_SDCARD(v) (v[0] == 's' && v[1] == 'd')
int GET_SLOT(char *path) {
	if(IS_SDCARD(path)) {
		return path[2] - 'a';
	}
	// IDE-EXI
	return path[3] - 'a';
}

file_handle initial_SD_A =
	{ "sda:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_SD_B =
	{ "sdb:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_SD_C =
	{ "sdc:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};	
	
file_handle initial_ATA_A =
	{ "ataa:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_ATA_B =
	{ "atab:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};
	
file_handle initial_ATA_C =
	{ "atac:/",       // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};

static device_info initial_FAT_info[5];
	
device_info* deviceHandler_FAT_info(file_handle* file) {
	int isSDCard = IS_SDCARD(file->name);
	int slot = GET_SLOT(file->name);
	return &initial_FAT_info[(isSDCard ? slot : SD_COUNT + slot)];
}

void populateDeviceInfo(file_handle* file) {
	device_info* info = deviceHandler_FAT_info(file);
	
	if(deviceHandler_getStatEnabled()) {
		int isSDCard = IS_SDCARD(file->name);
		int slot = GET_SLOT(file->name);
		
		sprintf(txtbuffer, "Reading filesystem info for %s%c:/", isSDCard ? "sd":"ata", ('a'+slot));
		print_gecko(txtbuffer);
		uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, txtbuffer));
		
		sprintf(txtbuffer, "%s%c:/", isSDCard ? "sd":"ata", ('a'+slot));
		DWORD freeClusters;
		FATFS *fatfs;
		if(f_getfree(txtbuffer, &freeClusters, &fatfs) == FR_OK) {
			info->freeSpace = (u64)(freeClusters) * fatfs->csize * fatfs->ssize;
			info->totalSpace = (u64)(fatfs->n_fatent - 2) * fatfs->csize * fatfs->ssize;
		}
		else {
			info->freeSpace = info->totalSpace = 0LL;
		}
		DrawDispose(msgBox);
	}
}
	
s32 deviceHandler_FAT_readDir(file_handle* ffile, file_handle** dir, u32 type) {	

	DIRF* dp = malloc(sizeof(DIRF));
	memset(dp, 0, sizeof(DIRF));
	if(f_opendir(dp, ffile->name) != FR_OK) return -1;
	FILINFO entry;
	
	// Set everything up to read
	int num_entries = 1, i = 1;
	*dir = calloc(sizeof(file_handle), 1);
	concat_path((*dir)[0].name, ffile->name, "..");
	(*dir)[0].fileAttrib = IS_SPECIAL;

	// Read each entry of the directory
	while( f_readdir(dp, &entry) == FR_OK && entry.fname[0] != '\0') {
		if(!swissSettings.showHiddenFiles && ((entry.fattrib & AM_HID) || entry.fname[0] == '.')) {
			continue;
		}
		// Do we want this one?
		if((type == -1 || ((entry.fattrib & AM_DIR) ? (type==IS_DIR) : (type==IS_FILE)))) {
			if(!(entry.fattrib & AM_DIR)) {
				if(!checkExtension(entry.fname)) continue;
			}
			// Make sure we have room for this one
			if(i == num_entries){
				++num_entries;
				*dir = realloc( *dir, num_entries * sizeof(file_handle) ); 
			}
			memset(&(*dir)[i], 0, sizeof(file_handle));
			concat_path((*dir)[i].name, ffile->name, entry.fname);
			(*dir)[i].size       = entry.fsize;
			(*dir)[i].fileAttrib = (entry.fattrib & AM_DIR) ? IS_DIR : IS_FILE;
			++i;
		}
	}
	
	f_closedir(dp);
	free(dp);
	return num_entries;
}

s64 deviceHandler_FAT_seekFile(file_handle* file, s64 where, u32 type){
	if(type == DEVICE_HANDLER_SEEK_SET) file->offset = where;
	else if(type == DEVICE_HANDLER_SEEK_CUR) file->offset = file->offset + where;
	else if(type == DEVICE_HANDLER_SEEK_END) file->offset = file->size + where;
	return file->offset;
}

s32 deviceHandler_FAT_readFile(file_handle* file, void* buffer, u32 length) {
  	if(!file->ffsFp) {
		file->ffsFp = malloc(sizeof(FIL));
		if(f_open(file->ffsFp, file->name, FA_READ ) != FR_OK) {
			free(file->ffsFp);
			file->ffsFp = NULL;
			return -1;
		}
	}
	if(file->size <= 0) {
		file->size = f_size(file->ffsFp);
	}
	
	f_lseek(file->ffsFp, file->offset);
	if(length > 0) {
		UINT bytes_read = 0;
		if(f_read(file->ffsFp, buffer, length, &bytes_read) != FR_OK || bytes_read != length) {
			return -1;
		}
		file->offset += bytes_read;
		return bytes_read;
	}
	return 0;
}

s32 deviceHandler_FAT_writeFile(file_handle* file, void* buffer, u32 length) {
	if(!file->ffsFp) {
		file->ffsFp = malloc(sizeof(FIL));
		if(f_open(file->ffsFp, file->name, FA_CREATE_ALWAYS | FA_WRITE ) != FR_OK) {
			free(file->ffsFp);
			file->ffsFp = NULL;
			return -1;
		}
	}
	f_lseek(file->ffsFp, file->offset);
		
	UINT bytes_written = 0;
	if(f_write(file->ffsFp, buffer, length, &bytes_written) != FR_OK || bytes_written != length) {
		return -1;
	}
	file->offset += bytes_written;
	return bytes_written;
}

s32 deviceHandler_FAT_setupFile(file_handle* file, file_handle* file2, int numToPatch) {
	int i;
	file_frag *fragList = NULL;
	u32 numFrags = 0;
	
	// Look for patch files, if we find some, open them and add them as fragments
	file_handle patchFile;
	for(i = 0; i < numToPatch; i++) {
		memset(&patchFile, 0, sizeof(file_handle));
		concatf_path(patchFile.name, devices[DEVICE_CUR]->initial->name, "swiss/patches/%.4s/%i", (char*)&GCMDisk, i);
		
		if(devices[DEVICE_CUR]->readFile(&patchFile, NULL, 0) == 0) {
			u32 patchInfo[4];
			memset(patchInfo, 0, 16);
			devices[DEVICE_CUR]->seekFile(&patchFile, -16, DEVICE_HANDLER_SEEK_END);
			if((devices[DEVICE_CUR]->readFile(&patchFile, &patchInfo, 16) == 16) && (patchInfo[2] == SWISS_MAGIC)) {
				if(!getFragments(DEVICE_CUR, &patchFile, &fragList, &numFrags, FRAGS_DISC_1, patchInfo[0], patchInfo[1])) {
					devices[DEVICE_CUR]->closeFile(&patchFile);
					free(fragList);
					return 0;
				}
				devices[DEVICE_CUR]->closeFile(&patchFile);
			}
			else {
				devices[DEVICE_CUR]->deleteFile(&patchFile);
				free(fragList);
				return 0;
			}
		}
		else {
			free(fragList);
			return 0;
		}
	}
	
	// If disc 1 is fragmented, make a note of the fragments and their sizes
	if(!getFragments(DEVICE_CUR, file, &fragList, &numFrags, FRAGS_DISC_1, 0, UINT32_MAX)) {
		free(fragList);
		return 0;
	}
	
	// If there is a disc 2 and it's fragmented, make a note of the fragments and their sizes
	if(file2) {
		// TODO fix 2 disc patched games
		if(!getFragments(DEVICE_CUR, file2, &fragList, &numFrags, FRAGS_DISC_2, 0, UINT32_MAX)) {
			free(fragList);
			return 0;
		}
	}
	
	if(swissSettings.igrType == IGR_BOOTBIN || endsWith(file->name,".tgc")) {
		memset(&patchFile, 0, sizeof(file_handle));
		concat_path(patchFile.name, devices[DEVICE_CUR]->initial->name, "swiss/patches/apploader.img");
		
		ApploaderHeader apploaderHeader;
		if(devices[DEVICE_CUR]->readFile(&patchFile, &apploaderHeader, sizeof(ApploaderHeader)) != sizeof(ApploaderHeader) || apploaderHeader.rebootSize != reboot_bin_size) {
			devices[DEVICE_CUR]->deleteFile(&patchFile);
			
			memset(&apploaderHeader, 0, sizeof(ApploaderHeader));
			apploaderHeader.rebootSize = reboot_bin_size;
			
			devices[DEVICE_CUR]->seekFile(&patchFile, 0, DEVICE_HANDLER_SEEK_SET);
			devices[DEVICE_CUR]->writeFile(&patchFile, &apploaderHeader, sizeof(ApploaderHeader));
			devices[DEVICE_CUR]->writeFile(&patchFile, reboot_bin, reboot_bin_size);
			devices[DEVICE_CUR]->closeFile(&patchFile);
		}
		
		getFragments(DEVICE_CUR, &patchFile, &fragList, &numFrags, FRAGS_APPLOADER, 0x2440, 0);
		devices[DEVICE_CUR]->closeFile(&patchFile);
	}
	
	if(swissSettings.emulateMemoryCard) {
		if(devices[DEVICE_CUR] != &__device_sd_a && devices[DEVICE_CUR] != &__device_ata_a && devices[DEVICE_CUR] != &__device_ata_c) {
			memset(&patchFile, 0, sizeof(file_handle));
			concatf_path(patchFile.name, devices[DEVICE_CUR]->initial->name, "swiss/patches/MemoryCardA.%s.raw", wodeRegionToString(GCMDisk.RegionCode));
			concatf_path(txtbuffer, devices[DEVICE_CUR]->initial->name, "swiss/saves/MemoryCardA.%s.raw", wodeRegionToString(GCMDisk.RegionCode));
			ensure_path(DEVICE_CUR, "swiss/saves", NULL);
			devices[DEVICE_CUR]->renameFile(&patchFile, txtbuffer);	// TODO remove this in our next major release
			
			if(devices[DEVICE_CUR]->readFile(&patchFile, NULL, 0) != 0) {
				devices[DEVICE_CUR]->seekFile(&patchFile, 16*1024*1024, DEVICE_HANDLER_SEEK_SET);
				devices[DEVICE_CUR]->writeFile(&patchFile, NULL, 0);
				devices[DEVICE_CUR]->closeFile(&patchFile);
			}
			
			if(getFragments(DEVICE_CUR, &patchFile, &fragList, &numFrags, FRAGS_CARD_A, 0, 31.5*1024*1024))
				*(vu8*)VAR_CARD_A_ID = (patchFile.size * 8/1024/1024) & 0xFC;
			devices[DEVICE_CUR]->closeFile(&patchFile);
		}
		
		if(devices[DEVICE_CUR] != &__device_sd_b && devices[DEVICE_CUR] != &__device_ata_b) {
			memset(&patchFile, 0, sizeof(file_handle));
			concatf_path(patchFile.name, devices[DEVICE_CUR]->initial->name, "swiss/patches/MemoryCardB.%s.raw", wodeRegionToString(GCMDisk.RegionCode));
			concatf_path(txtbuffer, devices[DEVICE_CUR]->initial->name, "swiss/saves/MemoryCardB.%s.raw", wodeRegionToString(GCMDisk.RegionCode));
			ensure_path(DEVICE_CUR, "swiss/saves", NULL);
			devices[DEVICE_CUR]->renameFile(&patchFile, txtbuffer);	// TODO remove this in our next major release
			
			if(devices[DEVICE_CUR]->readFile(&patchFile, NULL, 0) != 0) {
				devices[DEVICE_CUR]->seekFile(&patchFile, 16*1024*1024, DEVICE_HANDLER_SEEK_SET);
				devices[DEVICE_CUR]->writeFile(&patchFile, NULL, 0);
				devices[DEVICE_CUR]->closeFile(&patchFile);
			}
			
			if(getFragments(DEVICE_CUR, &patchFile, &fragList, &numFrags, FRAGS_CARD_B, 0, 31.5*1024*1024))
				*(vu8*)VAR_CARD_B_ID = (patchFile.size * 8/1024/1024) & 0xFC;
			devices[DEVICE_CUR]->closeFile(&patchFile);
		}
	}
	
	if(fragList) {
		print_frag_list(fragList, numFrags);
		*(vu32**)VAR_FRAG_LIST = installPatch2(fragList, (numFrags + 1) * sizeof(file_frag));
		free(fragList);
		fragList = NULL;
	}
	
	int isSDCard = IS_SDCARD(file->name);
	int slot = GET_SLOT(file->name);
	if(isSDCard) {
		// Card Type
		*(vu8*)VAR_SD_SHIFT = sdgecko_getAddressingType(slot) ? 9:0;
	}
	// Copy the actual freq
	*(vu8*)VAR_EXI_FREQ = isSDCard ? sdgecko_getSpeed(slot):(swissSettings.exiSpeed ? EXI_SPEED32MHZ:EXI_SPEED16MHZ);
	// Device slot (0, 1 or 2)
	*(vu8*)VAR_EXI_SLOT = slot;
	*(vu32**)VAR_EXI_REGS = ((vu32(*)[5])0xCC006800)[isSDCard ? (slot%EXI_CHANNEL_MAX):(slot%EXI_CHANNEL_2)];
	// IDE-EXI only settings
	if(!isSDCard) {
		// Is the HDD in use a 48 bit LBA supported HDD?
		*(vu8*)VAR_ATA_LBA48 = ataDriveInfo.lba48Support;
	}
	return 1;
}

s32 fatFs_Mount(u8 devNum, char *path) {
	if(fs[devNum] != NULL) {
		print_gecko("Unmount %i devnum, %s path\r\n", devNum, path);
		f_unmount(path);
		free(fs[devNum]);
		fs[devNum] = NULL;
		disk_shutdown(devNum);
	}
	fs[devNum] = (FATFS*)malloc(sizeof(FATFS));
	return f_mount(fs[devNum], path, 1) == FR_OK;
}

void setSDGeckoSpeed(int slot, bool fast) {
	sdgecko_setSpeed(slot, fast ? EXI_SPEED32MHZ:EXI_SPEED16MHZ);
	print_gecko("SD speed set to %s\r\n", (fast ? "32MHz":"16MHz"));
}

s32 deviceHandler_FAT_init(file_handle* file) {
	int isSDCard = IS_SDCARD(file->name);
	int slot = GET_SLOT(file->name);
	int ret = 0;
	print_gecko("Init %s %i\r\n", (isSDCard ? "SD":"ATA"), slot);
	// SD Card - Slot A
	if(isSDCard && slot == 0) {
		setSDGeckoSpeed(slot, swissSettings.exiSpeed);
		__device_sd_a.features |= FEAT_BOOT_GCM;
		ret = fatFs_Mount(DEV_SDA, "sda:/");
		if(!ret) {
			setSDGeckoSpeed(slot, false);
			__device_sd_a.features &= ~FEAT_BOOT_GCM;
			ret = fatFs_Mount(DEV_SDA, "sda:/");
		}
	}
	// SD Card - Slot B
	if(isSDCard && slot == 1) {
		setSDGeckoSpeed(slot, swissSettings.exiSpeed);
		__device_sd_b.features |= FEAT_BOOT_GCM;
		ret = fatFs_Mount(DEV_SDB, "sdb:/");
		if(!ret) {
			setSDGeckoSpeed(slot, false);
			__device_sd_b.features &= ~FEAT_BOOT_GCM;
			ret = fatFs_Mount(DEV_SDB, "sdb:/");
		}
	}
	// SD Card - SD2SP2
	if(isSDCard && slot == 2) {
		setSDGeckoSpeed(slot, swissSettings.exiSpeed);
		__device_sd_c.features |= FEAT_BOOT_GCM;
		ret = fatFs_Mount(DEV_SDC, "sdc:/");
		if(!ret) {
			setSDGeckoSpeed(slot, false);
			__device_sd_c.features &= ~FEAT_BOOT_GCM;
			ret = fatFs_Mount(DEV_SDC, "sdc:/");
		}
	}
	// IDE-EXI - Slot A
	if(!isSDCard && slot == 0) {
		ret = fatFs_Mount(DEV_ATAA, "ataa:/");
	}
	// IDE-EXI - Slot B
	if(!isSDCard && slot == 1) {
		ret = fatFs_Mount(DEV_ATAB, "atab:/");
	}
	// M.2 Loader
	if(!isSDCard && slot == 2) {
		ret = fatFs_Mount(DEV_ATAC, "atac:/");
	}
	if(ret)
		populateDeviceInfo(file);
	return ret;
}

s32 deviceHandler_FAT_closeFile(file_handle* file) {
	int ret = 0;
	if(file && file->ffsFp) {
		ret = f_close(file->ffsFp);
		free(file->ffsFp);
		file->ffsFp = NULL;
	}
	return ret;
}

s32 deviceHandler_FAT_deinit(file_handle* file) {
	deviceHandler_FAT_closeFile(file);
	device_info* info = deviceHandler_FAT_info(file);
	info->freeSpace = 0LL;
	info->totalSpace = 0LL;
	if(file) {
		int isSDCard = IS_SDCARD(file->name);
		int slot = GET_SLOT(file->name);
		f_unmount(file->name);
		free(fs[isSDCard ? slot : SD_COUNT+slot]);
		fs[isSDCard ? slot : SD_COUNT+slot] = NULL;
		disk_shutdown(isSDCard ? slot : SD_COUNT+slot);
	}
	return 0;
}

s32 deviceHandler_FAT_deleteFile(file_handle* file) {
	deviceHandler_FAT_closeFile(file);
	return f_unlink(file->name);
}

s32 deviceHandler_FAT_renameFile(file_handle* file, char* name) {
	deviceHandler_FAT_closeFile(file);
	int ret = f_rename(file->name, name);
	strcpy(file->name, name);
	return ret;
}

s32 deviceHandler_FAT_makeDir(file_handle* dir) {
	return f_mkdir(dir->name);
}

bool deviceHandler_FAT_test_sd_a() {
	return carda->startup() && carda->shutdown();
}
bool deviceHandler_FAT_test_sd_b() {
	return cardb->startup() && cardb->shutdown();
}
bool deviceHandler_FAT_test_sd_c() {
	return cardc->startup() && cardc->shutdown();
}
bool deviceHandler_FAT_test_ata_a() {
	return ide_exi_inserted(0);
}
bool deviceHandler_FAT_test_ata_b() {
	return ide_exi_inserted(1);
}
bool deviceHandler_FAT_test_ata_c() {
	return ide_exi_inserted(2);
}

u32 deviceHandler_FAT_emulated_sd() {
	if (swissSettings.audioStreaming)
		return EMU_READ | EMU_AUDIO_STREAMING | EMU_BUS_ARBITER;
	else if (swissSettings.emulateReadSpeed)
		return EMU_READ | EMU_READ_SPEED | EMU_BUS_ARBITER;
	else if (swissSettings.emulateMemoryCard)
		return EMU_READ | EMU_MEMCARD | EMU_BUS_ARBITER;
	else
		return EMU_READ | EMU_BUS_ARBITER;
}

u32 deviceHandler_FAT_emulated_ata() {
	if (swissSettings.audioStreaming)
		return EMU_READ | EMU_AUDIO_STREAMING | EMU_BUS_ARBITER;
	else if (swissSettings.emulateMemoryCard)
		return EMU_READ | EMU_MEMCARD | EMU_BUS_ARBITER;
	else
		return EMU_READ | EMU_BUS_ARBITER;
}

DEVICEHANDLER_INTERFACE __device_sd_a = {
	DEVICE_ID_1,
	"SD Card Adapter",
	"SD Card - Slot A",
	"SD(HC/XC) Card - Supported File System(s): FAT16, FAT32, exFAT",
	{TEX_SDSMALL, 59, 78, 64, 80},
	FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_AUTOLOAD_DOL|FEAT_FAT_FUNCS|FEAT_HYPERVISOR|FEAT_PATCHES|FEAT_AUDIO_STREAMING,
	EMU_READ|EMU_READ_SPEED|EMU_AUDIO_STREAMING|EMU_MEMCARD,
	LOC_MEMCARD_SLOT_A,
	&initial_SD_A,
	(_fn_test)&deviceHandler_FAT_test_sd_a,
	(_fn_info)&deviceHandler_FAT_info,
	(_fn_init)&deviceHandler_FAT_init,
	(_fn_makeDir)&deviceHandler_FAT_makeDir,
	(_fn_readDir)&deviceHandler_FAT_readDir,
	(_fn_seekFile)&deviceHandler_FAT_seekFile,
	(_fn_readFile)&deviceHandler_FAT_readFile,
	(_fn_writeFile)&deviceHandler_FAT_writeFile,
	(_fn_closeFile)&deviceHandler_FAT_closeFile,
	(_fn_deleteFile)&deviceHandler_FAT_deleteFile,
	(_fn_renameFile)&deviceHandler_FAT_renameFile,
	(_fn_setupFile)&deviceHandler_FAT_setupFile,
	(_fn_deinit)&deviceHandler_FAT_deinit,
	(_fn_emulated)&deviceHandler_FAT_emulated_sd,
};

DEVICEHANDLER_INTERFACE __device_sd_b = {
	DEVICE_ID_2,
	"SD Card Adapter",
	"SD Card - Slot B",
	"SD(HC/XC) Card - Supported File System(s): FAT16, FAT32, exFAT",
	{TEX_SDSMALL, 59, 78, 64, 80},
	FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_AUTOLOAD_DOL|FEAT_FAT_FUNCS|FEAT_HYPERVISOR|FEAT_PATCHES|FEAT_AUDIO_STREAMING,
	EMU_READ|EMU_READ_SPEED|EMU_AUDIO_STREAMING|EMU_MEMCARD,
	LOC_MEMCARD_SLOT_B,
	&initial_SD_B,
	(_fn_test)&deviceHandler_FAT_test_sd_b,
	(_fn_info)&deviceHandler_FAT_info,
	(_fn_init)&deviceHandler_FAT_init,
	(_fn_makeDir)&deviceHandler_FAT_makeDir,
	(_fn_readDir)&deviceHandler_FAT_readDir,
	(_fn_seekFile)&deviceHandler_FAT_seekFile,
	(_fn_readFile)&deviceHandler_FAT_readFile,
	(_fn_writeFile)&deviceHandler_FAT_writeFile,
	(_fn_closeFile)&deviceHandler_FAT_closeFile,
	(_fn_deleteFile)&deviceHandler_FAT_deleteFile,
	(_fn_renameFile)&deviceHandler_FAT_renameFile,
	(_fn_setupFile)&deviceHandler_FAT_setupFile,
	(_fn_deinit)&deviceHandler_FAT_deinit,
	(_fn_emulated)&deviceHandler_FAT_emulated_sd,
};

DEVICEHANDLER_INTERFACE __device_ata_a = {
	DEVICE_ID_3,
	"IDE-EXI",
	"IDE-EXI - Slot A",
	"IDE/PATA HDD - Supported File System(s): FAT16, FAT32, exFAT",
	{TEX_HDD, 104, 73, 112, 80},
	FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_AUTOLOAD_DOL|FEAT_FAT_FUNCS|FEAT_HYPERVISOR|FEAT_PATCHES|FEAT_AUDIO_STREAMING,
	EMU_READ|EMU_AUDIO_STREAMING|EMU_MEMCARD,
	LOC_MEMCARD_SLOT_A,
	&initial_ATA_A,
	(_fn_test)&deviceHandler_FAT_test_ata_a,
	(_fn_info)&deviceHandler_FAT_info,
	(_fn_init)&deviceHandler_FAT_init,
	(_fn_makeDir)&deviceHandler_FAT_makeDir,
	(_fn_readDir)&deviceHandler_FAT_readDir,
	(_fn_seekFile)&deviceHandler_FAT_seekFile,
	(_fn_readFile)&deviceHandler_FAT_readFile,
	(_fn_writeFile)&deviceHandler_FAT_writeFile,
	(_fn_closeFile)&deviceHandler_FAT_closeFile,
	(_fn_deleteFile)&deviceHandler_FAT_deleteFile,
	(_fn_renameFile)&deviceHandler_FAT_renameFile,
	(_fn_setupFile)&deviceHandler_FAT_setupFile,
	(_fn_deinit)&deviceHandler_FAT_deinit,
	(_fn_emulated)&deviceHandler_FAT_emulated_ata,
};

DEVICEHANDLER_INTERFACE __device_ata_b = {
	DEVICE_ID_4,
	"IDE-EXI",
	"IDE-EXI - Slot B",
	"IDE/PATA HDD - Supported File System(s): FAT16, FAT32, exFAT",
	{TEX_HDD, 104, 73, 112, 80},
	FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_AUTOLOAD_DOL|FEAT_FAT_FUNCS|FEAT_HYPERVISOR|FEAT_PATCHES|FEAT_AUDIO_STREAMING,
	EMU_READ|EMU_AUDIO_STREAMING|EMU_MEMCARD,
	LOC_MEMCARD_SLOT_B,
	&initial_ATA_B,
	(_fn_test)&deviceHandler_FAT_test_ata_b,
	(_fn_info)&deviceHandler_FAT_info,
	(_fn_init)&deviceHandler_FAT_init,
	(_fn_makeDir)&deviceHandler_FAT_makeDir,
	(_fn_readDir)&deviceHandler_FAT_readDir,
	(_fn_seekFile)&deviceHandler_FAT_seekFile,
	(_fn_readFile)&deviceHandler_FAT_readFile,
	(_fn_writeFile)&deviceHandler_FAT_writeFile,
	(_fn_closeFile)&deviceHandler_FAT_closeFile,
	(_fn_deleteFile)&deviceHandler_FAT_deleteFile,
	(_fn_renameFile)&deviceHandler_FAT_renameFile,
	(_fn_setupFile)&deviceHandler_FAT_setupFile,
	(_fn_deinit)&deviceHandler_FAT_deinit,
	(_fn_emulated)&deviceHandler_FAT_emulated_ata,
};

DEVICEHANDLER_INTERFACE __device_sd_c = {
	DEVICE_ID_F,
	"SD Card Adapter",
	"SD Card - SD2SP2",
	"SD(HC/XC) Card - Supported File System(s): FAT16, FAT32, exFAT",
	{TEX_SDSMALL, 59, 78, 64, 80},
	FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_AUTOLOAD_DOL|FEAT_FAT_FUNCS|FEAT_HYPERVISOR|FEAT_PATCHES|FEAT_AUDIO_STREAMING,
	EMU_READ|EMU_READ_SPEED|EMU_AUDIO_STREAMING|EMU_MEMCARD,
	LOC_SERIAL_PORT_2,
	&initial_SD_C,
	(_fn_test)&deviceHandler_FAT_test_sd_c,
	(_fn_info)&deviceHandler_FAT_info,
	(_fn_init)&deviceHandler_FAT_init,
	(_fn_makeDir)&deviceHandler_FAT_makeDir,
	(_fn_readDir)&deviceHandler_FAT_readDir,
	(_fn_seekFile)&deviceHandler_FAT_seekFile,
	(_fn_readFile)&deviceHandler_FAT_readFile,
	(_fn_writeFile)&deviceHandler_FAT_writeFile,
	(_fn_closeFile)&deviceHandler_FAT_closeFile,
	(_fn_deleteFile)&deviceHandler_FAT_deleteFile,
	(_fn_renameFile)&deviceHandler_FAT_renameFile,
	(_fn_setupFile)&deviceHandler_FAT_setupFile,
	(_fn_deinit)&deviceHandler_FAT_deinit,
	(_fn_emulated)&deviceHandler_FAT_emulated_sd,
};

DEVICEHANDLER_INTERFACE __device_ata_c = {
	DEVICE_ID_H,
	"M.2 Loader",
	"M.2 Loader",
	"M.2 SATA SSD - Supported File System(s): FAT16, FAT32, exFAT",
	{TEX_M2LOADER, 112, 54, 120, 64},
	FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_AUTOLOAD_DOL|FEAT_FAT_FUNCS|FEAT_HYPERVISOR|FEAT_PATCHES|FEAT_AUDIO_STREAMING,
	EMU_READ|EMU_READ_SPEED|EMU_AUDIO_STREAMING|EMU_MEMCARD,
	LOC_SERIAL_PORT_1,
	&initial_ATA_C,
	(_fn_test)&deviceHandler_FAT_test_ata_c,
	(_fn_info)&deviceHandler_FAT_info,
	(_fn_init)&deviceHandler_FAT_init,
	(_fn_makeDir)&deviceHandler_FAT_makeDir,
	(_fn_readDir)&deviceHandler_FAT_readDir,
	(_fn_seekFile)&deviceHandler_FAT_seekFile,
	(_fn_readFile)&deviceHandler_FAT_readFile,
	(_fn_writeFile)&deviceHandler_FAT_writeFile,
	(_fn_closeFile)&deviceHandler_FAT_closeFile,
	(_fn_deleteFile)&deviceHandler_FAT_deleteFile,
	(_fn_renameFile)&deviceHandler_FAT_renameFile,
	(_fn_setupFile)&deviceHandler_FAT_setupFile,
	(_fn_deinit)&deviceHandler_FAT_deinit,
	(_fn_emulated)&deviceHandler_FAT_emulated_sd,
};
