#include "eboot.h"
#include "system_mgr.h"
#include <systemctrl.h>

Eboot::Eboot(string path){
    
    size_t lastSlash = path.rfind("/", string::npos);
    size_t substrPos = path.rfind("/", lastSlash-1)+1;

    this->path = path;
    this->subtype = NULL;
    this->ebootName = path.substr(lastSlash+1, string::npos);
    this->name = path.substr(substrPos, lastSlash-substrPos);
    this->readHeader();
    this->icon0 = common::getImage(IMAGE_WAITICON);
}

Eboot::~Eboot(){
    if (icon0 && icon0 != common::getImage(IMAGE_NOICON) && icon0 != common::getImage(IMAGE_WAITICON))
        delete icon0;
}

void Eboot::readHeader(){
    FILE* fp = fopen(this->path.c_str(), "rb");
    fread(&header, 1, sizeof(PBPHeader), fp);
    fclose(fp);
}

void Eboot::loadIcon(){
    Image* icon = NULL;
    if ( header.magic == EBOOT_MAGIC && header.icon1_offset-header.icon0_offset)
        icon = new Image(this->path, YA2D_PLACE_RAM, this->header.icon0_offset);
    
    if (icon == NULL)
        sceKernelDelayThread(0);
    icon = (icon == NULL)? common::getImage(IMAGE_NOICON) : icon;
    icon->swizzle();
    this->icon0 = icon;
}
        
string Eboot::getEbootName(){
    return this->ebootName;
}

void Eboot::loadPics(){
    this->pic0 = NULL;
    this->pic1 = NULL;

    int size;

    if (header.magic != EBOOT_MAGIC) return;
    
    // grab pic0.png
    size = this->header.pic1_offset-this->header.pic0_offset;
    if (size)
        this->pic0 = new Image(this->path, YA2D_PLACE_RAM, this->header.pic0_offset);

    // grab pic1.png
    size = this->header.snd0_offset-this->header.pic1_offset;
    if (size)
        this->pic1 = new Image(this->path, YA2D_PLACE_RAM, this->header.pic1_offset);

}


void Eboot::loadAVMedia(){
    this->icon1 = NULL;
    this->snd0 = NULL;
    this->at3_size = 0;
    this->icon1_size = 0;

    int size;

    if (header.magic != EBOOT_MAGIC) return;

    // grab snd0.at3
    size = this->header.elf_offset-this->header.snd0_offset;
    if (size){
        this->snd0 = malloc(size);
        memset(this->snd0, 0, size);
        this->at3_size = size;
        this->readFile(this->snd0, this->header.snd0_offset, size);
    }

    // grab icon1.pmf
    size = this->header.pic0_offset-this->header.icon1_offset;
    if (size){
        this->icon1 = malloc(size);
        memset(this->icon1, 0, size);
        this->icon1_size = size;
        this->readFile(this->icon1, this->header.icon1_offset, size);
    }
}

void Eboot::readFile(void* dst, unsigned offset, unsigned size){
    FILE* src = fopen(this->path.c_str(), "rb");
    fseek(src, offset, SEEK_SET);
    fread(dst, size, 1, src);
    fclose(src);
}

int Eboot::getEbootType(const char* path){

    int ret = UNKNOWN_TYPE;

    if (strcasecmp("ms0:/PSP/GAME/UPDATE/EBOOT.PBP", path) == 0 || strcasecmp("ef0:/PSP/GAME/UPDATE/EBOOT.PBP", path) == 0 || strcasecmp(("ms0:/PSP/APPS/UPDATE/"VBOOT_PBP), path) == 0 )
        return TYPE_UPDATER;

    Eboot e(path);
    u32 size = e.header.icon0_offset - e.header.param_offset;
    if (size){

        unsigned char* sfo_buffer = (unsigned char*)malloc(size);
        e.readFile(sfo_buffer, e.header.param_offset, size);

        u16 categoryType = 0;
        int value_size = sizeof(categoryType);
        Entry::getSfoParam(sfo_buffer, size, "CATEGORY", (unsigned char*)(&categoryType), &value_size);

        free(sfo_buffer);

        switch(categoryType){
            case HMB_CAT:            ret = TYPE_HOMEBREW;    break;
            case PSN_CAT:            ret = TYPE_PSN;         break;
            case PS1_CAT:            ret = TYPE_POPS;        break;
            default:                                         break;
        }        
    }
    return ret;
}

string Eboot::fullEbootPath(string path, string app, bool scan_dlc){
    // Return the full path of a homebrew given only the homebrew name

    static const char* EBOOT_150 = "%/EBOOT.PBP"; // 1.50 homebrew
    static const char* EBOOT_PBP = "/EBOOT.PBP";  // Normal EBOOT
    static const char* EBOOT_ARK = "/"VBOOT_PBP;  // ARK EBOOT
    static const char* EBOOT_CEF = "/FBOOT.PBP";  // TN CEF EBOOT
    static const char* EBOOT_HBL = "/WMENU.BIN";  // VHBL EBOOT
    static const char* PBOOT_PBP = "/PBOOT.PBP";  // Update file
    static const char* PBOOT_DLC = "/PARAM.PBP";  // DLC file

    if (common::fileExists(app))
        return app; // it's already a full path

    else if (common::fileExists(path+app+EBOOT_150))
        return path+app+EBOOT_150; // 1.50 homebrew

    else if (common::fileExists(path+app+EBOOT_PBP))
        return path+app+EBOOT_PBP; // Normal EBOOT
    
        else if (common::fileExists(path+app+EBOOT_ARK))
        return path+app+EBOOT_ARK; // ARK EBOOT

    else if (common::fileExists(path+app+EBOOT_CEF))
        return path+app+EBOOT_CEF; // TN CEF EBOOT

    else if (common::fileExists(path+app+EBOOT_HBL))
        return path+app+EBOOT_HBL; // VHBL EBOOT
    
    else if (scan_dlc && common::fileExists(path+app+PBOOT_PBP))
        return path+app+PBOOT_PBP; // Update file
    
    else if (scan_dlc && common::fileExists(path+app+PBOOT_DLC))
        return path+app+PBOOT_DLC; // DLC file

    return "";
}

char* Eboot::getType(){
    return "EBOOT";
}

char* Eboot::getSubtype(){
    if (subtype == NULL){
        switch(getEbootType(this->path.c_str())){
        case TYPE_HOMEBREW: this->subtype = "HOMEBREW"; break;
        case TYPE_PSN: this->subtype = "PSN"; break;
        case TYPE_POPS: this->subtype = "POPS"; break;
        case TYPE_UPDATER: this->subtype = "UPDATER"; break;
        }
    }
    return this->subtype;
}

SfoInfo Eboot::getSfoInfo(){
    SfoInfo info = this->Entry::getSfoInfo();
    // grab PARAM.SFO
    u32 size = this->header.icon0_offset-this->header.param_offset;
    if (size){

        unsigned char* sfo_buffer = (unsigned char*)malloc(size);
        this->readFile(sfo_buffer, this->header.param_offset, size);

        int title_size = sizeof(info.title);
        Entry::getSfoParam(sfo_buffer, size, "TITLE", (unsigned char*)(info.title), &title_size);
        
        int id_size = sizeof(info.gameid);
        Entry::getSfoParam(sfo_buffer, size, "DISC_ID", (unsigned char*)(info.gameid), &id_size);

        free(sfo_buffer);
    }
    return info;
}

bool Eboot::isEboot(const char* path){
    return (common::getExtension(path) == "pbp" || strstr(path, "wmenu.bin"));
}

void Eboot::doExecute(){
    if (this->name == "Recovery Menu") common::launchRecovery(this->path.c_str());
    else Eboot::executeEboot(this->path.c_str());
}

void Eboot::executeUpdate(const char* path){
    struct SceKernelLoadExecVSHParam param;
    
    memset(&param, 0, sizeof(param));
    
    int runlevel = UPDATER_RUNLEVEL;
    
    param.args = strlen(path) + 1;
    param.argp = (char*)path;
    param.key = "updater";
    sctrlKernelLoadExecVSHWithApitype(runlevel, path, &param);
}


int loadReboot150()
{
    int k1 = pspSdkSetK1(0);
    SceUID mod = sceKernelLoadModule(ARK_DC_PATH "/150/reboot150.prx", 0, NULL);
    if (mod < 0) {
        pspSdkSetK1(k1);
        return mod;
    }

    int res = sceKernelStartModule(mod, 0, NULL, NULL, NULL);

    pspSdkSetK1(k1);

    return res;
}


void Eboot::executeHomebrew(const char* path){
    struct SceKernelLoadExecVSHParam param;
    
    memset(&param, 0, sizeof(param));
    
    int runlevel = (*(u32*)path == EF0_PATH && common::getConf()->redirect_ms0)? HOMEBREW_RUNLEVEL_GO : HOMEBREW_RUNLEVEL;
    
    param.args = strlen(path) + 1;
    param.argp = (char*)path;
    param.key = "game";

    // fix 1.50 homebrew
    char *perc = strchr(path, '%');
    if (perc) {
        strcpy(perc, perc + 1);
        //path = param.argp;
    }

    if(strstr(path, "ms0:/PSP/GAME150/") == path) {
        loadReboot150();
    }

    sctrlKernelLoadExecVSHWithApitype(runlevel, path, &param);
}

void Eboot::executePSN(const char* path){
    struct SceKernelLoadExecVSHParam param;
    
    memset(&param, 0, sizeof(param));
    param.argp = (char*)"disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
    param.key = "umdemu";
    sctrlSESetBootConfFileIndex(PSN_DRIVER);
    sctrlSESetUmdFile("");
    
    int runlevel = (*(u32*)path == EF0_PATH)? ISO_RUNLEVEL_GO : ISO_RUNLEVEL; // PSN games must always be redirected

    string pboot_path = string(path);
    pboot_path = pboot_path.substr(0, pboot_path.rfind('/')+1) + "PBOOT.PBP";
    if (common::fileExists(pboot_path)){
        runlevel = (*(u32*)path == EF0_PATH)? ISO_PBOOT_RUNLEVEL_GO : ISO_PBOOT_RUNLEVEL;
        param.argp = (void*)pboot_path.c_str();
    }

    param.args = strlen((char*)param.argp)+1;

    sctrlKernelLoadExecVSHWithApitype(runlevel, path, &param);
}

void Eboot::executePOPS(const char* path){
    struct SceKernelLoadExecVSHParam param;
    
    memset(&param, 0, sizeof(param));
    
    int runlevel = (*(u32*)path == EF0_PATH)? POPS_RUNLEVEL_GO : POPS_RUNLEVEL;
    
    param.args = strlen(path) + 1;
    param.argp = (char*)path;
    param.key = "pops";
    sctrlKernelLoadExecVSHWithApitype(runlevel, path, &param);
}

void Eboot::executeEboot(const char* path){
    if (common::getMagic(path, 0) == ELF_MAGIC){ // plain ELF (1.50) homebrew
        Eboot::executeHomebrew(path);
        return;
    }
    switch (Eboot::getEbootType(path)){
    case TYPE_HOMEBREW:    Eboot::executeHomebrew(path);    break;
    case TYPE_PSN:         Eboot::executePSN(path);         break;
    case TYPE_POPS:        Eboot::executePOPS(path);        break;
    case TYPE_UPDATER:     Eboot::executeUpdate(path);      break;
    }
}
