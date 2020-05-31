#include <switch.h>

Result launchProgram(u64 tid, u64 *pid) {
    NcmProgramLocation programLocation{
      .program_id = tid,
      .storageID = NcmStorageId_None,
    };
    return pmshellLaunchProgram(0, &programLocation, pid);
}

enum OverrideStatusFlag : u64 {
    OverrideStatusFlag_Hbl             = BIT(0),
    OverrideStatusFlag_ProgramSpecific = BIT(1),
    OverrideStatusFlag_CheatEnabled    = BIT(2),
};

typedef struct {
    u64 keys_held;
    u64 flags;

    #define DEFINE_FLAG_ACCESSORS(flag) \
    constexpr inline bool Is##flag() const { return this->flags & OverrideStatusFlag_##flag; } \
    constexpr inline void Set##flag() { this->flags |= OverrideStatusFlag_##flag; } \
    constexpr inline void Clear##flag() { this->flags &= ~u64(OverrideStatusFlag_##flag); }

    DEFINE_FLAG_ACCESSORS(Hbl)
    DEFINE_FLAG_ACCESSORS(ProgramSpecific)
    DEFINE_FLAG_ACCESSORS(CheatEnabled)

    #undef DEFINE_FLAG_ACCESSORS
} CfgOverrideStatus;

Result pmdmntAtmosphereGetProcessInfo(Handle* handle_out, NcmProgramLocation *loc_out, CfgOverrideStatus *status_out, u64 pid) {
    Handle tmp_handle;

    struct {
        NcmProgramLocation loc;
        CfgOverrideStatus status;
    } out;

    Result rc = serviceDispatchInOut(pmdmntGetServiceSession(), 65000, pid, out,
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = &tmp_handle,
    );

    if (R_SUCCEEDED(rc)) {
        if (handle_out) {
            *handle_out = tmp_handle;
        } else {
            svcCloseHandle(tmp_handle);
        }

        if (loc_out) *loc_out = out.loc;
        if (status_out) *status_out = out.status;
    }

    return rc;
}

// Borrowed from saltyNX https://github.com/shinyquagsire23/SaltyNX
typedef struct {
    u32 type;
    u32 flags;
    u64 threadId;
    union
    {
        // AttachProcess
        struct
        {
            u64 tid;
            u64 pid;
            char name[12];
            u32 isA64 : 1;
            u32 addrSpace : 3;
            u32 enableDebug : 1;
            u32 enableAslr : 1;
            u32 useSysMemBlocks : 1;
            u32 poolPartition : 4;
            u32 unused : 22;
            u64 userExceptionContextAddr;
        };

        // AttachThread
        struct
        {
           u64 attachThreadId;
           u64 tlsPtr;
           u64 entrypoint;
        };

        // ExitProcess/ExitThread
        struct
        {
            u32 exitType;
        };

        // Exception
        struct
        {
            u32 exceptionType;
            u32 faultRegister;
        };
    };
} DebugEventInfo;

inline Result svcGetDebugEventInfo(DebugEventInfo* event_out, Handle debug)
{
    return svcGetDebugEvent((u8*)event_out, debug);
}