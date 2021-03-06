#include "NX.hpp"

// Maximum number of titles to read using pdm
#define MAX_TITLES 2000

// Comparison of AccountUids
bool operator == (const AccountUid &a, const AccountUid &b) {
    if (a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1]) {
        return true;
    }
    return false;
}

// Here until libnx gets updated to support 10.0.0 (it's just copied from the commit that fixes the issue)
// Note this is exactly the same as what libnx will do - I just don't want to build against it when it's not a release, this is easier :P
Result pdmqryQueryRecentlyPlayedApplicationWorkaround(AccountUid uid, bool flag, u64 *application_ids, s32 count, s32 *total_out) {
    Service * pdmqrySrv = pdmqryGetServiceSession();
    if (hosversionBefore(10,0,0)) {
        return serviceDispatchInOut(pdmqrySrv, 14, uid, *total_out,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { application_ids, count*sizeof(u64) } },
        );
    }

    const struct {
        u8 flag;
        u8 pad[7];
        AccountUid uid;
    } in = { flag!=0, {0}, uid };

    return serviceDispatchInOut(pdmqrySrv, 14, in, *total_out,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
        .buffers = { { application_ids, count*sizeof(u64) } },
    );
}

namespace Utils::NX {
    ThemeType getHorizonTheme() {
        ColorSetId thm;
        Result rc = setsysGetColorSetId(&thm);
        if (R_SUCCEEDED(rc)) {
            switch (thm) {
                case ColorSetId_Light:
                    return ThemeType::Light;
                    break;

                case ColorSetId_Dark:
                    return ThemeType::Dark;
                    break;
            }
        }

        // If it fails return dark
        return ThemeType::Dark;
    }

    Language getSystemLanguage() {
        SetLanguage sl;
        u64 l;
        setInitialize();
        setGetSystemLanguage(&l);
        setMakeLanguage(l, &sl);
        setExit();

        Language lang;
        switch (sl) {
            case SetLanguage_ENGB:
            case SetLanguage_ENUS:
                lang = English;
                break;

            case SetLanguage_FR:
                lang = French;
                break;

            case SetLanguage_DE:
                lang = German;
                break;

            case SetLanguage_IT:
                lang = Italian;
                break;

            case SetLanguage_PT:
                lang = Portugese;
                break;

            case SetLanguage_RU:
                lang = Russian;
                break;

            default:
                lang = Default;
                break;
        }

        return lang;
    }

    ::NX::User * getUserPageUser() {
        ::NX::User * u = nullptr;

        AppletType t = appletGetAppletType();
        if (t == AppletType_LibraryApplet) {
            // Attempt to get user id from IStorage
            AppletStorage * s = (AppletStorage *)malloc(sizeof(AppletStorage));
            // Pop common args IStorage
            if (R_SUCCEEDED(appletPopInData(s))) {
                // Pop MyPage-specific args IStorage
                if (R_SUCCEEDED(appletPopInData(s))) {
                    // Get user id
                    AccountUid uid;
                    appletStorageRead(s, 0x8, &uid, 0x10);

                    // Check if valid
                    AccountUid userIDs[ACC_USER_LIST_SIZE];
                    s32 num = 0;
                    accountListAllUsers(userIDs, ACC_USER_LIST_SIZE, &num);
                    for (s32 i = 0; i < num; i++) {
                        if (uid == userIDs[i]) {
                            u = new ::NX::User(uid);
                            break;
                        }
                    }
                }
            }
            free(s);
        }

        return u;
    }

    std::vector<::NX::User *> getUserObjects() {
        // Get IDs
        std::vector<::NX::User *> users;
        AccountUid userIDs[ACC_USER_LIST_SIZE];
        s32 num = 0;
        Result rc = accountListAllUsers(userIDs, ACC_USER_LIST_SIZE, &num);

        if (R_SUCCEEDED(rc)) {
            // Create objects and insert into vector
            for (s32 i = 0; i < num; i++) {
                users.push_back(new ::NX::User(userIDs[i]));
            }
        }

        // Returns an empty vector if an error occurred
        return users;
    }

    std::vector<::NX::Title *> getTitleObjects(std::vector<::NX::User *> u) {
        Result rc;

        // Get ALL played titles for ALL users
        // (this doesn't include installed games that haven't been played)
        std::vector<TitleID> playedIDs;
        for (unsigned short i = 0; i < u.size(); i++) {
            s32 playedTotal = 0;
            TitleID * tmpIDs = new TitleID[MAX_TITLES];
            pdmqryQueryRecentlyPlayedApplicationWorkaround(u[i]->ID(), false, tmpIDs, MAX_TITLES, &playedTotal);

            // Push back ID if not already in the vector
            for (s32 j = 0; j < playedTotal; j++) {
                bool found = false;
                for (size_t k = 0; k < playedIDs.size(); k++) {
                    if (playedIDs[k] == tmpIDs[j]) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    playedIDs.push_back(tmpIDs[j]);
                }
            }
            delete[] tmpIDs;
        }

        // Get IDs of all installed titles
        std::vector<TitleID> installedIDs;
        NsApplicationRecord * records = new NsApplicationRecord[MAX_TITLES];
        s32 count = 0;
        s32 installedTotal = 0;
        while (true){
            rc = nsListApplicationRecord(records, MAX_TITLES, count, &installedTotal);
            // Break if at the end or no titles
            if (R_FAILED(rc) || installedTotal == 0){
                break;
            }
            count++;
            installedIDs.push_back(records->application_id);
        }
        delete[] records;

        // Create Title objects from IDs
        std::vector<::NX::Title *> titles;
        for (size_t i = 0; i < playedIDs.size(); i++) {
            // Loop over installed titles to determine if installed or not
            bool installed = false;
            for (size_t j = 0; j < installedIDs.size(); j++) {
                if (installedIDs[j] == playedIDs[i]) {
                    installed = true;
                    break;
                }
            }

            titles.push_back(new ::NX::Title(playedIDs[i], installed));
        }

        return titles;
    }

    void startServices() {
        accountInitialize(AccountServiceType_System);
        nsInitialize();
        pdmqryInitialize();
        romfsInit();
        setsysInitialize();
        socketInitializeDefault();

        #if _NXLINK_
            nxlinkStdio();
        #endif
    }

    void stopServices() {
        accountExit();
        nsExit();
        pdmqryExit();
        romfsExit();
        setsysExit();
        socketExit();
    }
};