#include <pspsdk.h>
#include <pspkernel.h>
#include <pspreg.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <ark.h>
#include <stdio.h>

#include "systemctrl.h"

PSP_MODULE_INFO("ARK_Uninstaller", 0x0800, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_VSH);

#define printf pspDebugScreenPrintf

int main(int argc, char *args[]) {
    ARKConfig*  ark_config = (ARKConfig*)ARK_CONFIG;
    pspDebugScreenInit();
    SceCtrlData pad;
    sctrlArkGetConfig(ark_config);
    if(IS_VITA(ark_config) || IS_VITA_ADR(ark_config)) {
        printf("Sorry just works on the PSP for now...\n");
        sceKernelDelayThread(3000000);
        sceKernelExitGame();
    }
    if(strcasecmp(ark_config->exploit_id, CIPL_EXPLOIT_ID) == 0 || strcasecmp(ark_config->exploit_id, "Infinity") == 0) {
        pspDebugScreenSetTextColor(0xFF0000FF);
        printf("\nYou need to remove cIPL/Infinity before uninstalling!!!!\n");
        printf("\nExiting back to XMB ...");
        sceKernelDelayThread(8000000);
        sceKernelExitGame();
    }
    printf("It is safest to just use ChronoSwitch to reinstall OFW without ARK-4 modules.\n\nPress X to continue\nPress O to quit...\n");
    while(1) {
        sceCtrlReadBufferPositive(&pad, 1);
        if(pad.Buttons & PSP_CTRL_CROSS)
        	break;
        else if(pad.Buttons & PSP_CTRL_CIRCLE)
        	sceKernelExitGame();
    }
    pspDebugScreenClear();

    SceUID dir = sceIoDopen("ef0:/PSP/SAVEDATA/ARK_01234");
    int go = 1;    
    if (dir<0) {
        dir = sceIoDopen("ms0:/PSP/SAVEDATA/ARK_01234");
        go = 0;
    }
    if(dir<0) {
        printf("Can't find ARK_01234 folder... Exiting....\n");
        sceKernelDelayThread(3000000);
        sceKernelExitGame();
    }

    char fullpath[60];
    if(go)
        snprintf(fullpath, sizeof(fullpath), "%s", "ef0:/PSP/SAVEDATA/ARK_01234/");
    else
        snprintf(fullpath, sizeof(fullpath), "%s", "ms0:/PSP/SAVEDATA/ARK_01234/");

    printf("Removing ARK_01234");
    SceIoDirent dirent;
    SceUID arkDir;
    memset(&dirent, 0, sizeof(SceIoDirent));
    while((arkDir = sceIoDread(dir, &dirent)) > 0) {
        if(strcmp(dirent.d_name, ".") == 0 || strcmp(dirent.d_name, "..") == 0)
        	continue;
        else {
        	char fp[60] = {0};
        	snprintf(fp, sizeof(fp), "%s%s", fullpath, dirent.d_name);
        	sceIoRemove(fp);
        	printf("%s\n", fp);
        }
    }

    sceIoDclose(dir);
    if (go) {
        sceIoRmdir("ef0:/PSP/SAVEDATA/ARK_01234");
        sceIoRemove("ef0:/PSP/GAME/ARK_Loader/EBOOT.PBP");
        sceIoRemove("ef0:/PSP/GAME/ARK_Loader/K.BIN");
        sceIoRmdir("ef0:/PSP/GAME/ARK_Loader");
    }
    else {
        sceIoRmdir("ms0:/PSP/SAVEDATA/ARK_01234");
        sceIoRemove("ms0:/PSP/GAME/ARK_Loader/EBOOT.PBP");
        sceIoRemove("ms0:/PSP/GAME/ARK_Loader/K.BIN");
        sceIoRmdir("ms0:/PSP/GAME/ARK_Loader");
    }


    sceIoUnassign("flash0:");
    sceIoAssign("flash0:", "lflash0:0,0", "flashfat0:", IOASSIGN_RDWR, NULL, 0);
    sceIoRemove("flash0:/kd/ark_systemctrl.prx");
    sceIoRemove("flash0:/kd/ark_vshctrl.prx");
    sceIoRemove("flash0:/kd/ark_inferno.prx");
    sceIoRemove("flash0:/kd/ark_stargate.prx");
    sceIoRemove("flash0:/kd/ark_popcorn.prx");
    sceIoRemove("flash0:/kd/ark_pspcompat.prx");
    asm("sync"::);
    printf("Removing Flash0 6 files...\n");
    printf("flash0:/kd/ark_systemctrl.prx\n");
    printf("flash0:/kd/ark_vshctrl.prx\n");
    printf("flash0:/kd/ark_inferno.prx\n");
    printf("flash0:/kd/ark_stargate.prx\n");
    printf("flash0:/kd/ark_popcorn.prx\n");
    printf("flash0:/kd/ark_pspcompat.prx\n");

    sceKernelDelayThread(3000000);

    scePowerRequestColdReset(0);

    return 0;
}
