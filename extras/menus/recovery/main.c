#include <pspsdk.h>
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <ark.h>

#include <main.h>

#define SCREEN_WIDTH 58
#define SCREEN_HEIGHT 33 

#define printf pspDebugScreenPrintf

//STMOD_HANDLER previous;

PSP_MODULE_INFO("Recovery", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(4096);

int psp_model;
ARKConfig _arkconf;
ARKConfig* ark_config = &_arkconf;
CFWConfig config;
SEConfig se_config;
int is_launcher_mode = 0;

extern int usb_is_enabled;
extern void USB_enable();
extern void USB_disable();
extern int proshell_main();

void* vsh_malloc(size_t size){
    SceUID uid = sceKernelAllocPartitionMemory(2, "", PSP_SMEM_High, size+sizeof(u32), NULL);
    int* ptr = sceKernelGetBlockHeadAddr(uid);
    if (ptr){
        ptr[0] = uid;
        return &(ptr[1]);
    }
    return NULL;
}

void vsh_free(int* ptr){
    if (ptr){
        int uid = ptr[-1];
        sceKernelFreePartitionMemory(uid);
    }
}

void* malloc(size_t size){
    return vsh_malloc(size);
}

void free(void* ptr){
    vsh_free(ptr);
}

SceUID get_thread_id(const char *name)
{
    int ret, count, i;
    SceUID ids[128];

    ret = sceKernelGetThreadmanIdList(SCE_KERNEL_TMID_Thread, ids, sizeof(ids), &count);

    if(ret < 0) {
    	return -1;
    }

    for(i=0; i<count; ++i) {
    	SceKernelThreadInfo info;

    	info.size = sizeof(info);
    	ret = sceKernelReferThreadStatus(ids[i], &info);

    	if(ret < 0) {
    		continue;
    	}

    	if(0 == strcmp(info.name, name)) {
    		return ids[i];
    	}
    }

    return -2;
}

void suspend_thread(const char *thread_name)
{
    int ret;

    ret = get_thread_id(thread_name);

    sceKernelSuspendThread(ret);
}

void suspend_vsh_thread(void)
{
    sceDisplayWaitVblankStart();
    suspend_thread("SCE_VSH_GRAPHICS");
    suspend_thread("movie_player");
    suspend_thread("audio_buffer");
    suspend_thread("music_player");
}

static char* findRecoveryApp(){
    const char *p = "ms0:/PSP/GAME/RECOVERY/EBOOT.PBP";
    SceIoStat stat;
    if (sceIoGetstat(p, &stat) < 0){
        p = "ef0:/PSP/GAME/RECOVERY/EBOOT.PBP";
        if (sceIoGetstat(p, &stat) < 0) return NULL;
    }
    return p;
}

static int launchRecoveryApp(char* p){
    struct SceKernelLoadExecVSHParam param;

    int apitype = 0x141;

    memset(&param, 0, sizeof(param));
    param.size = sizeof(param);
    param.args = strlen(p) + 1;
    param.argp = p;
    param.key = "game";
    sctrlKernelLoadExecVSHWithApitype(apitype, p, &param);

    // SHOULD NOT REALLY GET HERE
    return 0;
}

static void checkArkPath(){
    int fd;

    fd = sceIoDopen("ef0:/PSP/SAVEDATA/ARK_01234");
    if (fd >= 0){
        strcpy(ark_config->arkpath, "ef0:/PSP/SAVEDATA/ARK_01234/");
        sceIoDclose(fd);
        return;
    }

    fd = sceIoDopen("ms0:/PSP/SAVEDATA/ARK_01234");
    if (fd >= 0){
        strcpy(ark_config->arkpath, "ms0:/PSP/SAVEDATA/ARK_01234/");
        sceIoDclose(fd);
        return;
    }

    fd = sceIoDopen("ms0:/SEPLUGINS");
    if (fd >= 0) {
        strcpy(ark_config->arkpath, "ms0:/SEPLUGINS/");
        sceIoDclose(fd);
        return;
    }
    strcpy(ark_config->arkpath, "flash1:/");

}

static int selected_choice(u32 choice) {
    int ret;

    switch (choice){

    case 0:
        pspDebugScreenSetXY(25, 30);
        printf("Good-bye ;-)");
        sceKernelDelayThread(100000);
        return 0;
    case 1:
        //TODO USB Toggle
        pspDebugScreenSetXY(25, 30);
        if (usb_is_enabled){
            printf("Disabling USB...");
            USB_disable();
        }
        else{
            printf("Enabling USB...");
            USB_enable();
        }
        sceKernelDelayThread(1000000);
        return 1;
    case 2:
        checkArkPath();
        loadSettings();
        settings_submenu();
        saveSettings();
        return 1;
    case 3:
        checkArkPath();
        loadPlugins();
        plugins_submenu();
        savePlugins();
        return 1;
    case 4:
        proshell_main();
        return 1;
    case 5:
        {
            char* p = findRecoveryApp();
            pspDebugScreenSetXY(20, 30);
            if (p) printf("Booting %s", p);
            else printf("Not found :(");
            sceKernelDelayThread(2000000);
            if (p){
                launchRecoveryApp(p);
                return 0;
            }
            return 1;
        }
    }

}

static void draw(char** options, int size, int dir){
    pspDebugScreenSetXY(0, 1);
    pspDebugScreenSetTextColor(0xFFD800);
    printf("********************************************************************");

    pspDebugScreenSetXY(0, 2);
    printf("* ARK-4 Recovery Menu *                                            *");
    pspDebugScreenSetXY(0, 3);
    printf("***********************                                            *");
    pspDebugScreenSetXY(0, 4);
    printf("*                                                                  *");

    for (int i=0; i<=size; i++){
        pspDebugScreenSetTextColor(0xFFD800);
        pspDebugScreenSetXY(0, 5 + 2*i);
        printf("* ");
        if (dir != i){
            pspDebugScreenSetTextColor(0xFFFFFF);
        }
        printf(options[i]);
        pspDebugScreenSetTextColor(0xFFD800);
        int len = strlen(options[i])+2;
        int padding = 67 - len;
        for (int j=0; j<padding; j++) printf(" ");
        printf("*");

        pspDebugScreenSetXY(0, 6 + 2*i);            
        printf("*                                                                  *");
    }

    // ADD SIDE BORDERS
    for (int i=pspDebugScreenGetY(); i<SCREEN_HEIGHT; i++) {
        pspDebugScreenSetXY(0, i);
        printf("*                                                                  *");
    }

    // BOTTOM BORDER
    pspDebugScreenSetXY(0, 33);
    printf("********************************************************************");
}

int main(SceSize args, void *argp) {

    suspend_vsh_thread();
    pspDebugScreenInit();

    if (is_launcher_mode){
        sceKernelDelayThread(10000);
        proshell_main();
    }

    SceCtrlData pad;
    char *options[] = {
        "Exit",
        "Toggle USB",
        "Custom Firmware Settings",
        "Plugins Manager",
        "PRO Shell",
        "Run /PSP/GAME/RECOVERY/EBOOT.PBP"
    };

    char* usb_options[] = {
        "Toggle USB - Memory Stick",
        "Toggle USB - flash0",
        "Toggle USB - flash1",
        "Toggle USB - flash2",
        "Toggle USB - flash3",
        "Toggle USB - UMD",
    };
    int n_usb = sizeof(usb_options)/sizeof(usb_options[0]);

    if (IS_PSP(ark_config)){
        if (psp_model == PSP_GO){
            usb_options[0] = "Toggle USB - Internal Memory"; // replace ms with ef
            n_usb--; // remove UMD
        }
        options[1] = usb_options[0];
    }

    int size = (sizeof(options) / sizeof(options[0]))-1;
    int dir = 0;

    draw(options, size, dir);

    while(1) {

        sceDisplayWaitVblankStart();

        sceCtrlPeekBufferPositive(&pad, 1);
        
        // CONTROLS
        if (pad.Buttons & PSP_CTRL_DOWN) {
            sceKernelDelayThread(200000);
        	dir++;
        	if(dir>size) dir = 0;

            draw(options, size, dir);
        }
        if (pad.Buttons & PSP_CTRL_UP) {
            sceKernelDelayThread(200000);
        	dir--;
        	if(dir<0) dir = size;
            
            draw(options, size, dir);
        }
        if ((pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE))) {
            sceKernelDelayThread(200000);
            int ret = selected_choice(dir);
            if(ret==0) break;
            
            draw(options, size, dir);
        }
        if (pad.Buttons & PSP_CTRL_LEFT){
            if (dir == 1 && IS_PSP(ark_config)){
                sceKernelDelayThread(200000);
                if (se_config.usbdevice == 0) se_config.usbdevice = n_usb-1;
                else se_config.usbdevice--;
            
                options[1] = usb_options[se_config.usbdevice];
                draw(options, size, dir);
            }
        }
        if (pad.Buttons & PSP_CTRL_RIGHT){
            if (dir == 1 && IS_PSP(ark_config)){
                sceKernelDelayThread(200000);
                if (se_config.usbdevice < n_usb-1) se_config.usbdevice++;
                else se_config.usbdevice = 0;

                options[1] = usb_options[se_config.usbdevice];
                draw(options, size, dir);
            }
        }
    }

    sceKernelExitGame();
    return 0;
}

int module_start(int argc, void* argv){

    psp_model = kuKernelGetModel();
    sctrlHENGetArkConfig(ark_config);
    sctrlSEGetConfig(&se_config);
    is_launcher_mode = (strcmp(ark_config->launcher, "PROSHELL") == 0);

    int uid = sceKernelCreateThread("ClassicRecovery", main, 16 - 1, 32*1024, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(uid, 0, NULL);
}
