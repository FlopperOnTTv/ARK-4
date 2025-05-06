#include <sstream>
#include <pspkernel.h>
#include <psppower.h>
#include <kubridge.h>
#include <systemctrl.h>

#include "system_mgr.h"
#include "common.h"
#include "controller.h"
#include "music_player.h"

string ark_version = "";

static SceUID draw_thread = -1;
static SceUID draw_sema = -1;
static bool running = true;
static bool system_menu = false;

/* State of the options Menu drawing function and animation state
    optionsDrawState has 4 possible values
    0: draw closed menu
    1: draw popup animation
    2: draw menu
    3: draw popout animation
*/
static int optionsDrawState = 0;
static int optionsAnimState; // state of the animation
static int optionsTextAnim; // -1 for no animation, other for animation
static int screensaver = 0;
static int fullscreen = 0;
static int last_size; // Used to see if the user switched menu sizes

// options menu entries position of the entries
static int pEntryIndex;
static int cur_entry = 0;
static int page_start = 0;

static int volume = 0;
static int mute = 0;
static clock_t volume_time = 0;
//static clock_t mute_time = 0;

static int MAX_ENTRIES = 0;
static SystemEntry** entries = NULL;

struct EntryScale {
    float font_size;
    int icon_scale;
    int dialog_height;
};

static struct EntryScale entry_scaling[3] = {
    {SIZE_LITTLE, 52, 90},
    {SIZE_MEDIUM, 72, 110},
    {SIZE_BIG, 100, 140}
};

static bool stillLoading(){
    for (int i=0; i<MAX_ENTRIES; i++){
        if (entries[i]->isStillLoading())
            return true;
    }
    return false;
}

static int getSizeIndex(){
    int menuSize = common::getConf()->menusize % 3;

    // Maps 0 -> 2
    //      1 -> 0
    //      2 -> 1
    return (menuSize + 2) % 3;
}

static int getNumPageItems(){
    return 5 - getSizeIndex(); // 5 for small, 4 for medium, and 3 for large
}

void SystemMgr::changeMenuState(){
    if (optionsDrawState == 1 || optionsDrawState == 3)
        return;

    int menu_size = getSizeIndex();
    if (menu_size != last_size) {
        page_start = max(0, pEntryIndex - getNumPageItems() + 1);
        last_size = menu_size;
    }

    common::playMenuSound();

    if (system_menu){
        optionsAnimState = 0;
        optionsDrawState = 3;
        system_menu = false;
    }
    else{
        optionsTextAnim = -1;
        optionsAnimState = -120;
        optionsDrawState = 1;
        system_menu = true;
    }
    
}


static void systemController(Controller* pad){
    if (optionsDrawState != 2)
        return;

    if (pad->accept()){
        entries[cur_entry]->pause();
        SystemMgr::changeMenuState();
        cur_entry = pEntryIndex;
        entries[cur_entry]->resume();
    }
    else if (pad->decline()){
        SystemMgr::changeMenuState();
        pEntryIndex = cur_entry;
        page_start = max(0, cur_entry - getNumPageItems() + 1);
    }
    else if (pad->left()){
        if (pEntryIndex == 0)
            return;

        pEntryIndex--;

        if (pEntryIndex == page_start && page_start > 0){
            page_start--;
        }

        common::playMenuSound();
    }
    else if (pad->right()){
        if (pEntryIndex == (MAX_ENTRIES-1))
            return;

        pEntryIndex++;
        
        if (pEntryIndex - page_start >= getNumPageItems()){
            page_start++;
        }

        common::playMenuSound();
    }
}

static void drawOptionsMenuCommon(){
    int menu_size = getSizeIndex();
    struct EntryScale scale_info = entry_scaling[menu_size];
    int dialog_height = scale_info.dialog_height;

    common::getImage(IMAGE_DIALOG)->draw_scale(0, optionsAnimState, 480, dialog_height); // Draw the background UI

    int offset = (480 - (MAX_ENTRIES * 15)) / 2;
    for (int i = 0; i < MAX_ENTRIES; i++){
        if (i == pEntryIndex){
            common::printText(offset + (i+1)*15, 15, "*", LITEGRAY, SIZE_BIG, true);
        } else{
            common::printText(offset + (i+1)*15, 15, "*", LITEGRAY, SIZE_LITTLE);
        }
    }    
    
    int num_items = getNumPageItems();
    int page_end = min(page_start + num_items, MAX_ENTRIES);

    int x_step = 480 / num_items;
    int x = (x_step - scale_info.icon_scale) / 2; // Initial margin

    static TextScroll scroll;
    for (int i = page_start; i < page_end; i++){
        SystemEntry *curr_entry = entries[i];

        // Icon vertical components
        int icon_height = curr_entry->getIcon()->getHeight();
        int scaled_height = (int)((float)icon_height * ((float)scale_info.icon_scale / icon_height));
        int icon_center_y = dialog_height / 2 - scaled_height / 2;

        // Possibly consolidate this branch into a single statement by enforcing strict dimension scaling for LARGE icons
        if(menu_size == 2) {
            curr_entry->getIcon()->draw(x, optionsAnimState + icon_center_y); // LARGE
        }
        else {
            curr_entry->getIcon()->draw_scale(x, optionsAnimState + icon_center_y, scale_info.icon_scale, scale_info.icon_scale); // MEDIUM & SMALL
        }

        if (i == pEntryIndex && optionsDrawState == 2){
            const char* entname = curr_entry->getName().c_str();
            int icon_width = curr_entry->getIcon()->getWidth();
            int scaled_width = (int)((float)icon_width * ((float)scale_info.icon_scale / icon_width));

            // Center text under icon
            int font_height = (int)((float)common::getFont()->texYSize * scale_info.font_size) / 4; // Divide by 4 because font heights can be aggressive. Sacrifices perfect centering
            int text_y = dialog_height - (dialog_height - (icon_center_y + scale_info.icon_scale)) / 2;
            int text_x = (x + scaled_width / 2) - common::calcTextWidth(entname, scale_info.font_size, 1) / 2; 
            scroll.w = 480 - text_x;
            
            // Some fonts have huge glyph heights, so we clamp them
            int max_text_dist = dialog_height - text_y;
            int clamped_text_y = text_y + font_height > text_y + max_text_dist ? text_y + max_text_dist / 2 : text_y + font_height;

            common::printText(text_x, clamped_text_y, entname, LITEGRAY, scale_info.font_size, 1, &scroll);
        }

        x += x_step;
    }
}

static void drawDateTime() {
    pspTime date;
    sceRtcGetCurrentClockLocalTime(&date);

    char dateStr[20];
    snprintf(dateStr, 20, "%04d/%02d/%02d %02d:%02d:%02d", date.year, date.month, date.day, date.hour, date.minutes, date.seconds);
    int x = 445 - common::calcTextWidth(dateStr, SIZE_MEDIUM, 0);
    if (common::getConf()->battery_percent) x -= common::calcTextWidth("-100%", SIZE_MEDIUM, 0);
    common::printText(x, 13, dateStr, LITEGRAY, SIZE_MEDIUM, 0, 0, 0);
}

static void drawBattery(){
    if (scePowerIsBatteryExist()) {
        int percent = scePowerGetBatteryLifePercent();
        
        if (percent < 0)
            return;

        u32 color;
        if (scePowerIsBatteryCharging()){
            color = BLUE;
        } else if (percent == 100){
            color = GREEN;
        } else if (percent >= 17){
            color = LITEGRAY;
        } else{
            color = RED;
        }

        if (common::getConf()->battery_percent) {
            char batteryPercent[5];
            snprintf(batteryPercent, 5, "%d%%", percent);
            common::printText(450-common::calcTextWidth(batteryPercent, SIZE_MEDIUM, 0), 13, batteryPercent, color, SIZE_MEDIUM, 0, 0, 0);
        }

        ya2d_draw_rect(455, 6, 20, 8, color, 0);
        ya2d_draw_rect(454, 8, 1, 5, color, 1);
        ya2d_draw_pixel(475, 14, color);
        
        if (percent >= 5){
            int width = percent*17/100;
            ya2d_draw_rect(457+(17-width), 8, width, 5, color, 1);
        }
    }
}

static void drawMute() {
    common::getIcon(FILE_MUSIC)->draw( common::getConf()->battery_percent ? 240:280, 3);
    int i = 2;
    for(;i<18;i++) {
        ya2d_draw_rect(common::getConf()->battery_percent ? 235+i:275+i, i, 1, 1, RED, 1); // Volume background outline
        ya2d_draw_rect(common::getConf()->battery_percent ? 255-i:295-i, i, 1, 1, RED, 1); // Volume background outline
    }
}

static void drawVolume(){
    const int max_slider_width = 480 / 4;
    const int volume_y = 255;

    const int outer_hndl_w = 3, outer_hndle_h = 6; // Inner handle will just be -1 these dimensions

    int slider_width = max(0, (int)((float)max_slider_width * ((float)volume / 30)) - outer_hndl_w);
    const int slider_x = (480 / 2) - (max_slider_width / 2);

    ya2d_draw_rect(slider_x - 1, volume_y - 1, max_slider_width + 1, 3, BLACK, 0); // Volume background outline

    ya2d_draw_rect(slider_x, volume_y, max_slider_width, 2, GRAY, 1); // Slider background
    ya2d_draw_rect(slider_x, volume_y, slider_width, 2, WHITE, 1); // Slider

    ya2d_draw_rect(slider_x + slider_width, volume_y - outer_hndle_h / 2, outer_hndl_w, outer_hndle_h, RED, 0); // Outer slider handle
    ya2d_draw_rect(slider_x + slider_width + 1, volume_y - outer_hndle_h / 2 + 1, outer_hndl_w - 1, outer_hndle_h - 1, WHITE, 1); // Inner slider handle
}

static void systemDrawer(){
    switch (optionsDrawState){
        case 0:
            // draw border, battery and datetime
            common::getImage(IMAGE_DIALOG)->draw_scale(0, 0, 480, 20);
            drawBattery();
            drawDateTime();
            // draw entry text
            entries[cur_entry]->drawInfo();
            // draw music icon is music player is open
            if (MusicPlayer::isPlaying()){
                common::getIcon(FILE_MUSIC)->draw( common::getConf()->battery_percent ? 240:280, 3);
            }
            if(mute)
                drawMute();
            break;
        case 1: // draw opening animation
            drawOptionsMenuCommon();
            optionsAnimState += 20;
            if (optionsAnimState > 0)
                optionsDrawState = 2;
            break;
        case 2: // draw menu
            optionsAnimState = 0;
            drawOptionsMenuCommon();
            break;
        case 3: // draw closing animation
            drawOptionsMenuCommon();
            optionsAnimState -= 20;
            if (optionsAnimState < -120)
                optionsDrawState = 0;
            break;
    }

    clock_t volume_elapsed = clock() - volume_time;
    double time_elapsed = ((double)volume_elapsed) / CLOCKS_PER_SEC;

    // Only show volume for 3 seconds and protect against clock_t wrap-around
    if (volume_time && time_elapsed < 3 && volume_elapsed < volume_time){
        drawVolume();
    }


}

void SystemMgr::drawScreen(){
	if(common::getConf()->main_menu != 0) {
        common::drawScreen();
	}
	else {
    	if (stillLoading()){
        	common::getImage(IMAGE_BG)->draw(0, 0);
    	}
    	else{
        	common::drawScreen();
    	}
	}
    if (!screensaver){
        entries[cur_entry]->draw();
        if (!fullscreen){
            systemDrawer();
            if (common::getConf()->show_fps){
                ostringstream fps;
                ya2d_calc_fps();
                fps << ya2d_get_fps();
                common::printText(460, 260, fps.str().c_str());
            }
        }
    }
}

static void *_sceImposeGetParam; // int (*)(int)
static void *_sceImposeSetParam; // int (*)(int, int)

static int drawThread(SceSize _args, void *_argp){
    common::stopLoadingThread();
    struct KernelCallArg args;
    args.arg1 = 0x8;
    kuKernelCall(_sceImposeGetParam, &args);
    mute = args.ret1;
    while (running){
        sceKernelWaitSema(draw_sema, 1, NULL);
        common::clearScreen(CLEAR_COLOR);
        SystemMgr::drawScreen();
        common::flipScreen();
        sceKernelSignalSema(draw_sema, 1);
        sceKernelDelayThread(0);
    }
    sceKernelExitDeleteThread(0);
    return 0;
}


static int controlThread(SceSize _args, void *_argp){
    static int screensaver_times[] = {0, 5, 10, 20, 30, 60};
    Controller pad;
    clock_t last_pressed = clock();

    while (running){
        int screensaver_time = screensaver_times[common::getConf()->screensaver];
        pad.update();

        if (pad.triangle() && !screensaver){
            SystemMgr::changeMenuState();
        } else if (pad.home()){
            screensaver = !screensaver;
            pad.flush();
            last_pressed = clock();
            continue;
        } else if (pad.volume() && _sceImposeGetParam != NULL && _sceImposeSetParam != NULL) {
            struct KernelCallArg args;
            args.arg1 = 0x1; // PSP_IMPOSE_MAIN_VOLUME

            kuKernelCall(_sceImposeGetParam, &args);
            int new_volume = args.ret1;

            volume_time = clock();

            // Unfortunate impose driver fix
            // Impose will sometimes register an extra volume input press in the opposite direction
            u32 buttons = pad.get_buttons();

            if (buttons & 0x100000 && new_volume < volume){
                args.arg2 = ++new_volume;
                kuKernelCall(_sceImposeSetParam, &args);
            } else if (buttons & 0x200000 && new_volume > volume){
                args.arg2 = --new_volume;
                kuKernelCall(_sceImposeSetParam, &args);
            }
            if (new_volume != volume){
                volume = new_volume;
                common::playMenuSound();
            }
        } else if (pad.mute() && _sceImposeGetParam != NULL && _sceImposeSetParam != NULL) {
            struct KernelCallArg args;
            args.arg1 = 0x8; // PSP_IMPOSE_MUTE

            kuKernelCall(_sceImposeGetParam, &args);
            mute = args.ret1;

//            mute_time = clock();

            // Unfortunate impose driver fix
            // Impose will sometimes register an extra volume input press in the opposite direction
            u32 buttons = pad.get_buttons();

            if(buttons & 0x800000) {
                args.arg1 = 0x8; // PSP_IMPOSE_MUTE
                args.arg2 = mute;
                kuKernelCall(_sceImposeSetParam, &args);
                //pad.update();
                if(mute) {
                    drawMute();
                }
            }

        } else if (!screensaver){
            if (system_menu) systemController(&pad);
            else entries[cur_entry]->control(&pad);
        }

        if (pad.any()){
            last_pressed = clock();
            if (screensaver){
                screensaver = 0;
                continue;
            }
        }

        if (screensaver_time > 0 && !stillLoading()){
            clock_t elapsed = clock() - last_pressed;
            double time_taken = ((double)elapsed)/CLOCKS_PER_SEC;
            if (time_taken > screensaver_time){
                screensaver = 1;
            }
        } else if (stillLoading()){
            last_pressed = clock();
        }

        sceKernelDelayThread(0);
    }

    sceKernelExitDeleteThread(0);
    return 0;
}


void SystemMgr::initMenu(SystemEntry** e, int ne){
    draw_sema = sceKernelCreateSema("draw_sema", 0, 1, 1, NULL);
    entries = e;
    MAX_ENTRIES = ne;
    last_size = getSizeIndex();

    // get ARK version    
    u32 ver = sctrlHENGetVersion(); // ARK's full version number
    u32 major = (ver&0xFF000000)>>24;
    u32 minor = (ver&0xFF0000)>>16;
    u32 micro = (ver&0xFF00)>>8;
    u32 rev   = sctrlHENGetMinorVersion();

    // get OFW version (bypass patches)
    struct KernelCallArg args;
    u32 getDevkitVersion = sctrlHENFindFunction("sceSystemMemoryManager", "SysMemUserForUser", 0x3FC9AE6A);    
    kuKernelCall((void*)getDevkitVersion, &args);
    u32 fw = args.ret1;
    u32 fwmajor = fw>>24;
    u32 fwminor = (fw>>16)&0xF;
    u32 fwmicro = (fw>>8)&0xF;

    _sceImposeGetParam = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", 0xDC3BECFF);
    _sceImposeSetParam = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", 0x3C318569);

    stringstream version;
    version << "" << fwmajor << "." << fwminor << fwmicro;
    version << " ARK " << major << "." << minor;
    if (micro>9) version << "." << micro;
    else if (micro>0) version << ".0" << micro;
    if (rev) version << " r" << rev;
    version << " " << common::getArkConfig()->exploit_id;
    #ifdef DEBUG
    version << " DEBUG";
    #endif
    ark_version = version.str();
}

void SystemMgr::startMenu(){
    draw_thread = sceKernelCreateThread("draw_thread", &drawThread, 0x10, 0x10000, PSP_THREAD_ATTR_VSH|PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(draw_thread, 0, NULL);
    entries[cur_entry]->resume();
    controlThread(0, NULL);
}

void SystemMgr::stopMenu(){
    running = false;
    sceKernelWaitThreadEnd(draw_thread, NULL);
    sceKernelTerminateDeleteThread(draw_thread);
}

void SystemMgr::endMenu(){
    for (int i=0; i<MAX_ENTRIES; i++) delete entries[i];
}

void SystemMgr::pauseDraw(){
    sceKernelWaitSema(draw_sema, 1, NULL);
}

void SystemMgr::resumeDraw(){
    int ret;
    do {
        ret = sceKernelSignalSema(draw_sema, 1);
    }while(ret != 0);
}

void SystemMgr::enterFullScreen(){
    fullscreen = 1;
}

void SystemMgr::exitFullScreen(){
    fullscreen = 0;
}

SystemEntry* SystemMgr::getSystemEntry(unsigned index){
    return (index < MAX_ENTRIES)? entries[index] : NULL;
}

void SystemMgr::setSystemEntry(SystemEntry* entry, unsigned index){
    if (index < MAX_ENTRIES) entries[index] = entry;
}

