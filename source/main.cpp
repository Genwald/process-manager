#define TESLA_INIT_IMPL // If you have more than one file using the tesla header, only define this in the main one
#include <tesla.hpp>    // The Tesla Header
#include <stdarg.h>
#include <stdint.h>
#include <filesystem>
#include <set>

#include <utils.h>

std::string stringFormat(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string stringFormat(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list args2;
    va_copy(args2, args);
    int size = 1 + vsnprintf(nullptr, 0, format, args);
    char* cString = new char[size];
    va_end(args);
    vsnprintf(cString, size, format, args2);
    va_end(args2);
    std::string string(cString);
    delete[] cString;
    return string;
}

class SmallText: public tsl::elm::Element {
public:
    SmallText(const std::string& text): Element(), text(text) { }
    virtual ~SmallText() {}
    virtual void draw(tsl::gfx::Renderer *renderer) override {
        renderer->drawString(text.c_str(), false, this->getX(), this->getY()+10, 18, a(tsl::style::color::ColorText));
    }
    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(this->getX(), this->getY()+6, this->getWidth(), 21);
    }
protected:
    std::string text;
};

class DisplayBar: public tsl::elm::Element {
public:
    DisplayBar(s16 value) : m_value(value) { }
    virtual ~DisplayBar() {}

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        u16 handlePos = (this->getWidth() - 95) * static_cast<float>(this->m_value) / 100;
        renderer->drawRect(this->getX() + handlePos, this->getY() + 5, this->getWidth() - 95 - handlePos, 5, a(tsl::style::color::ColorFrame));
        renderer->drawRect(this->getX(), this->getY() + 5, handlePos, 5, a(tsl::style::color::ColorHighlight));
    }
    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        this->setBoundaries(this->getX(), this->getY(), this->getWidth(), 21);
    }
protected:
    s16 m_value;
};

typedef struct {
    Result DebugActiveProcess;
    Result AtmosGetProcessInfo;
    Result GetInfo;
} infoResults;

typedef struct {
    u64 pid;
    DebugEventInfo debugInfo;
    u64 memoryAvailable;
    u64 memoryUsed;
    CfgOverrideStatus overrideStatus;
    NcmProgramLocation loc;
} processInfo;

bool getProcessInfo(u64 pid, processInfo &out, infoResults &results) {
    Result res;
    Handle handle;
    out.pid = pid;
    // fails on tesla and prcoessess with cheats
    results.DebugActiveProcess = svcDebugActiveProcess(&handle, pid);
    if(R_FAILED(res)) {
        svcCloseHandle(handle);
    }
    else {
        svcGetDebugEventInfo(&out.debugInfo, handle);
        svcCloseHandle(handle);
    }

    results.AtmosGetProcessInfo = pmdmntAtmosphereGetProcessInfo(&handle, &out.loc, &out.overrideStatus, pid);

    results.GetInfo = svcGetInfo(&out.memoryAvailable, InfoType_TotalMemorySize, handle, 0);
    if(R_FAILED(results.GetInfo)) {
        //list->addItem(new SmallText(stringFormat("svcGetInfo %x", res)));
    }
    svcGetInfo(&out.memoryUsed, InfoType_UsedMemorySize, handle, 0);
    svcCloseHandle(handle);
    return true;
}

class DetailsMenu : public tsl::Gui {
public:
    processInfo info;
    infoResults results;
    u64 pid;
    Handle handle;
    DetailsMenu(u64 pid):
    pid(pid) {
        getProcessInfo(pid, info, results);
    }

    virtual tsl::elm::Element* createUI() override {
        u64 tid = 0;
        tsl::elm::OverlayFrame *frame;
        if(R_SUCCEEDED(results.DebugActiveProcess)) {
            tid = info.debugInfo.tid;
            frame = new tsl::elm::OverlayFrame(info.debugInfo.name, stringFormat("%lx", tid));
        }
        else if(R_SUCCEEDED(results.AtmosGetProcessInfo)) {
            tid = info.loc.program_id;
            frame = new tsl::elm::OverlayFrame("???", stringFormat("%lx", tid));
        }
        else {
            frame = new tsl::elm::OverlayFrame("???", "???");
        }
        auto list = new tsl::elm::List();
        if(R_FAILED(results.DebugActiveProcess)) {
            list->addItem(new SmallText(stringFormat("svcDebugActiveProcess 0x%x", results.DebugActiveProcess)));
        }
        if(R_FAILED(results.AtmosGetProcessInfo)) {
            list->addItem(new SmallText(stringFormat("pmdmntAtmosphereGetProcessInfo 0x%x", results.AtmosGetProcessInfo)));
        }
        if(R_FAILED(results.GetInfo)) {
            list->addItem(new SmallText(stringFormat("svcGetInfo 0x%x", results.GetInfo)));
        }
        list->addItem(new tsl::elm::CategoryHeader("Info"));
        list->addItem(new SmallText(stringFormat("Process ID:       0x%lx", info.pid)));
        if(tid != 0) {
            list->addItem(new SmallText(stringFormat("Title ID:         %lx", tid)));
        }
        if(R_SUCCEEDED(results.DebugActiveProcess)) {
            list->addItem(new SmallText(std::string("32 bit:           ") + (!info.debugInfo.isA64 ? "True":"False")));
            std::string memPool;
            switch(info.debugInfo.poolPartition) {
                case 0 : memPool = "Application";
                break;
                case 1 : memPool = "Applet";
                break;
                case 2 : memPool = "Sysmodule";
                break;
                case 3 : memPool = "GPU";
            }
            if(!memPool.empty() && info.pid > 8) {
                list->addItem(new SmallText(stringFormat("Memory partition:  %s", memPool.c_str())));
            }
        }
        if(R_SUCCEEDED(results.GetInfo)) {
            list->addItem(new SmallText(stringFormat("Memory Used:  %lx/%lx", info.memoryUsed, info.memoryAvailable)));
            list->addItem(new DisplayBar(info.memoryUsed*100/info.memoryAvailable));
        }
        /* does not mean what I thought it meant
        if(info.overrideStatus.flags != UINT64_MAX) {
            list->addItem(new SmallText(std::string("Content Overriden: ") + (info.overrideStatus.IsProgramSpecific() ? "True":"False")));
            list->addItem(new SmallText(std::string("Cheats enabled:     ") + (info.overrideStatus.IsCheatEnabled() ? "True":"False")));
        }
        */
        if(R_SUCCEEDED(results.AtmosGetProcessInfo)) {
            std::string storageLoc;
            switch(info.loc.storageID) {
                case NcmStorageId_None          : storageLoc = "None";
                break;
                case NcmStorageId_Host          : storageLoc = "Host";
                break;
                case NcmStorageId_GameCard      : storageLoc = "Game Card";
                break;
                case NcmStorageId_BuiltInSystem : storageLoc = "System";
                break;
                case NcmStorageId_BuiltInUser   : storageLoc = "User";
                break;
                case NcmStorageId_SdCard        : storageLoc = "SD Card";
                break;
                case NcmStorageId_Any           : storageLoc = "Any";
            }
            // todo: better sort out prcoesses that give an innacurate result
            if(!storageLoc.empty()) {
                list->addItem(new SmallText(std::string("Storage Location:   ") + storageLoc));
            }
        }

        list->addItem(new tsl::elm::CategoryHeader("options"));
        if(tid != 0) {
            auto *terminateOption = new tsl::elm::ListItem("Terminate");
            terminateOption->setClickListener([tid](u64 keys) {
                if (keys & KEY_A) {
                    pmshellTerminateProgram(tid);
                    return true;
                }
                return false;
            });
            list->addItem(terminateOption);

            auto *launchOption = new tsl::elm::ListItem("Launch");
            launchOption->setClickListener([this, tid](u64 keys) {
                if (keys & KEY_A) {
                    // changing pid could cause an issue
                    launchProgram(tid, &info.pid);
                    return true;
                }
                return false;
            });
            list->addItem(launchOption);
        }

        if(R_SUCCEEDED(results.DebugActiveProcess)) {
            auto *pauseOption = new tsl::elm::ListItem("Pause");
            pauseOption->setClickListener([this](u64 keys) {
                if (keys & KEY_A) {
                    svcDebugActiveProcess(&handle, info.pid);
                    return true;
                }
                return false;
            });
            list->addItem(pauseOption);

            // Need some way to unpause after re-opening
            // These options make the top info un-viewable
            auto *unpauseOption = new tsl::elm::ListItem("Unpause");
            unpauseOption->setClickListener([this](u64 keys) {
                if (keys & KEY_A) {
                    svcCloseHandle(handle);
                    return true;
                }
                return false;
            });
            list->addItem(unpauseOption);
        }

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {
        // not sure how to update properly
        // TODO: update values on this screen
    }
};

class ProcessesMenu : public tsl::Gui {
public:
    std::string frameTitle;
    std::string frameSubtitle;
    ProcessesMenu(std::string frameTitle, std::string frameSubtitle): frameTitle(frameTitle), frameSubtitle(frameSubtitle) { }

    virtual bool tidInRange(u64 tid) = 0;

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame(frameTitle, frameSubtitle);
        auto list = new tsl::elm::List();

        u64 pids[0x200];
        s32 num = 0;
        svcGetProcessList(&num, pids, 0x200);
        processInfo info;
        infoResults results;
        u64 pid;

        for(int i=0; i<num; i++) {
            pid = pids[i];
            if(!getProcessInfo(pid, info, results)) {
                continue;
            }
            //Include/exclude system processes
            if(R_SUCCEEDED(results.AtmosGetProcessInfo)) {
                if(!tidInRange(info.loc.program_id)) {
                    continue;
                }
            }
            else if(!tidInRange(info.debugInfo.tid)) {
                continue;
            }

            tsl::elm::ListItem *processListItem;
            if(R_FAILED(results.DebugActiveProcess)) {
                processListItem = new tsl::elm::ListItem(stringFormat("??? [%lx]", info.loc.program_id));
            }
            else {
                processListItem = new tsl::elm::ListItem(stringFormat("%s [%lx]", info.debugInfo.name, info.debugInfo.tid));
            }
            processListItem->setClickListener([pid](u64 keys) {
                if (keys & KEY_A) {
                    tsl::changeTo<DetailsMenu>(pid);
                    return true;
                }
                return false;
            });
            list->addItem(processListItem);
        }

        frame->setContent(list);
        return frame;
    }

    // Called once every frame to update values
    virtual void update() override {

    }

    // Called once every frame to handle inputs not handled by other UI elements
    virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput, JoystickPosition leftJoyStick, JoystickPosition rightJoyStick) override {
        return false;   // Return true here to singal the inputs have been consumed
    }
};

class SystemProcessesMenu : public ProcessesMenu {
public:
    SystemProcessesMenu(): ProcessesMenu("System processes", "Terminating these may cause crashes") { }
    virtual inline bool tidInRange(u64 tid) override final {
        return tid <= 0x010000000000FFFF;
    }
};

class UserProcessesMenu : public ProcessesMenu {
public:
    UserProcessesMenu(): ProcessesMenu("User processes", "This includes applications and custom sys-modules") { }
    virtual inline bool tidInRange(u64 tid) override final {
        return tid > 0x010000000000FFFF;
    }
};

class LaunchMenu : public tsl::Gui {
protected:
    // Is this the best contianer? Does it matter at this size?
    std::set<u64> noLaunchList {
        0x0100000000000008,
        0x0100000000000032,
        0x0100000000000036,
    };
public:
    LaunchMenu() { }

    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Launch a sysmodule", "");
        auto list = new tsl::elm::List();

        std::error_code ec;
        u64 tid;
        for(auto& dirEnt: std::filesystem::directory_iterator("sdmc:/atmosphere/contents/", ec)) {
            if(std::filesystem::exists(dirEnt.path().string() + "/exefs.nsp")) {
                tid = strtoul(dirEnt.path().filename().c_str(), nullptr, 16);
                // Don't let users try to launch defualt sysmodules or a running process
                if(noLaunchList.find(tid) == noLaunchList.end() && R_FAILED(pmdmntGetProcessId(nullptr, tid))) {
                    //list->addItem(new SmallText(stringFormat("pmdmntGetProcessId %x", res)));
                    auto *launchOption = new tsl::elm::ListItem(dirEnt.path().filename().string());
                    launchOption->setClickListener([tid](u64 keys) {
                        if (keys & KEY_A) {
                            launchProgram(tid, nullptr);
                            return true;
                        }
                        return false;
                    });
                    list->addItem(launchOption);
                }
            }
        }
        if(ec) {
            list->addItem(new SmallText(stringFormat("error:  %s", ec.message().c_str())));
        }

        frame->setContent(list);

        return frame;
    }

    // Called once every frame to update values
    virtual void update() override {

    }

    // Called once every frame to handle inputs not handled by other UI elements
    virtual bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput, JoystickPosition leftJoyStick, JoystickPosition rightJoyStick) override {
        return false;   // Return true here to singal the inputs have been consumed
    }
};

class MainMenu : public tsl::Gui {
public:
    MainMenu() { }
    virtual tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Process Manager", "v0.0.1");
        auto list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Running Processes"));

        auto *userListItem = new tsl::elm::ListItem("User processes");
        userListItem->setClickListener([](u64 keys) {
           if (keys & KEY_A) {
               tsl::changeTo<UserProcessesMenu>();
               return true;
           }

           return false;
       });
       list->addItem(userListItem);

        auto *systemListItem = new tsl::elm::ListItem("System processes");
        systemListItem->setClickListener([](u64 keys) {
           if (keys & KEY_A) {
               tsl::changeTo<SystemProcessesMenu>();
               return true;
           }

           return false;
       });
       list->addItem(systemListItem);

       list->addItem(new tsl::elm::CategoryHeader("Other Processes"));

       auto *launchListItem = new tsl::elm::ListItem("Launch a process");
       launchListItem->setClickListener([](u64 keys) {
          if (keys & KEY_A) {
              tsl::changeTo<LaunchMenu>();
              return true;
          }

          return false;
       });
       list->addItem(launchListItem);

       frame->setContent(list);
       return frame;
    }
};

class OverlayTest : public tsl::Overlay {
public:
                                             // libtesla already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys
    virtual void initServices() override {
        pminfoInitialize();
        pmshellInitialize();
        fsdevMountSdmc();
    }  // Called at the start to initialize all services necessary for this Overlay
    virtual void exitServices() override {
        pminfoExit();
        pmshellExit();
        fsdevUnmountDevice("sdmc");
    }  // Callet at the end to clean up all services previously initialized

    virtual void onShow() override {}    // Called before overlay wants to change from invisible to visible state
    virtual void onHide() override {}    // Called before overlay wants to change from visible to invisible state

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainMenu>();  // Initial Gui to load. It's possible to pass arguments to it's constructor like this
    }
};

int main(int argc, char **argv) {
    return tsl::loop<OverlayTest>(argc, argv);
}