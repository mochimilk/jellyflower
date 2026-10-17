// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace acrylic {
enum class Language : unsigned { English, French, Korean, Count };
// Display strings only: file names, registry values, diagnostics, package IDs,
// URLs and the jellyflower/creator names deliberately remain stable.
#define JELLYFLOWER_TEXT(X) \
 X(Empty, L"", L"", L"") \
 X(System, L"system", L"système", L"시스템") \
 X(Light, L"light", L"clair", L"라이트") \
 X(Dark, L"dark", L"sombre", L"다크") \
 X(Auto, L"auto", L"auto", L"자동") \
 X(Taskbar, L"taskbar", L"barre des tâches", L"작업 표시줄") \
 X(Wallpaper, L"wallpaper", L"fond d’écran", L"배경 화면") \
 X(Cursor, L"cursor", L"curseur", L"커서") \
 X(Colors, L"colors", L"couleurs", L"색상") \
 X(Unlock, L"unlock", L"débloquer", L"잠금 해제") \
 X(General, L"general", L"général", L"일반") \
 X(Exit, L"exit", L"quitter", L"종료") \
 X(Acrylic, L"acrylic", L"acrylique", L"아크릴") \
 X(Pure, L"pure", L"pur", L"순수") \
 X(Date, L"date", L"date", L"날짜") \
 X(AutoHide, L"auto hide", L"masquer automatiquement", L"자동 숨기기") \
 X(Theme, L"theme", L"thème", L"테마") \
 X(On, L"on", L"activé", L"켬") \
 X(Off, L"off", L"désactivé", L"끔") \
 X(DefaultSystem, L"default (system)", L"par défaut (système)", L"기본값 (시스템)") \
 X(Disable, L"disable", L"désactiver", L"사용 안 함") \
 X(Blueberry, L"blueberry", L"myrtille", L"블루베리") \
 X(GreenTea, L"green tea", L"thé vert", L"녹차") \
 X(Mint, L"mint", L"menthe", L"민트") \
 X(Strawberry, L"strawberry", L"fraise", L"딸기") \
 X(ThaiTea, L"thai tea", L"thé thaï", L"타이 티") \
 X(Vanilla, L"vanilla", L"vanille", L"바닐라") \
 X(Yogurt, L"yogurt", L"yaourt", L"요거트") \
 X(LocationOff, L"location off", L"localisation désactivée", L"위치 꺼짐") \
 X(NoLocation, L"no location", L"position indisponible", L"위치 없음") \
 X(TryAgain, L"try again", L"réessayer", L"다시 시도") \
 X(Checking, L"checking...", L"vérification...", L"확인 중...") \
 X(Downloading, L"downloading...", L"téléchargement...", L"다운로드 중...") \
 X(Installing, L"installing...", L"installation...", L"설치 중...") \
 X(UpToDate, L"up to date", L"à jour", L"최신 버전") \
 X(Download, L"download", L"télécharger", L"다운로드") \
 X(NoRelease, L"no release yet", L"aucune version publiée", L"출시된 버전 없음") \
 X(ImageMissing, L"image missing", L"image introuvable", L"이미지 없음") \
 X(NotAllowed, L"not allowed", L"non autorisé", L"허용되지 않음") \
 X(FileInUse, L"file in use", L"fichier utilisé", L"파일 사용 중") \
 X(StorageFull, L"storage full", L"espace insuffisant", L"저장 공간 부족") \
 X(ImageTooLarge, L"image too large", L"image trop volumineuse", L"이미지가 너무 큼") \
 X(BackupIssue, L"backup issue", L"problème de sauvegarde", L"백업 문제") \
 X(ErrorCode, L"error {code}", L"erreur {code}", L"오류 {code}") \
 X(Search, L"search", L"rechercher", L"검색") \
 X(Morning, L"Good morning", L"Bonjour", L"좋은 아침이에요") \
 X(Afternoon, L"Good afternoon", L"Bon après-midi", L"좋은 오후예요") \
 X(Evening, L"Good evening", L"Bonsoir", L"좋은 저녁이에요") \
 X(GreetingNamed, L"{greeting}, {name} {emoji}", L"{greeting}, {name} {emoji}", L"{name}님, {greeting} {emoji}") \
 X(GreetingAnonymous, L"{greeting} {emoji}", L"{greeting} {emoji}", L"{greeting} {emoji}") \
 X(TrayTip, L"ur cute :3", L"t’es trop chou :3", L"너 귀여워 :3") \
 X(CursorSaveError, L"cursor preference could not be saved", L"préférence du curseur non enregistrée", L"커서 설정을 저장하지 못했어요") \
 X(CursorChangeError, L"cursor could not be changed", L"impossible de changer le curseur", L"커서를 변경하지 못했어요") \
 X(CursorMissingBody, L"Choose colors > unlock for ZIP download and import instructions. Windows error: {error}.", L"Choisissez couleurs > débloquer pour télécharger et importer le ZIP. Erreur Windows : {error}.", L"색상 > 잠금 해제에서 ZIP 다운로드 및 가져오기 안내를 확인하세요. Windows 오류: {error}.") \
 X(CursorDeniedBody, L"Windows denied the operation or could not load this cursor. Use your normal Windows desktop session. Windows error: {error}.", L"Windows a refusé l’opération ou n’a pas pu charger ce curseur. Utilisez votre session Windows habituelle. Erreur Windows : {error}.", L"Windows에서 작업을 거부했거나 커서를 불러오지 못했어요. 일반 Windows 데스크톱 세션에서 사용하세요. Windows 오류: {error}.") \
 X(ThemeNotifyError, L"theme changed; apps could not be notified", L"thème modifié ; applications non averties", L"테마는 변경됐지만 앱에 알리지 못했어요") \
 X(ThemeChangeError, L"windows mode could not be changed", L"impossible de changer le mode Windows", L"Windows 모드를 변경하지 못했어요") \
 X(ThemePartialBody, L"The system and app modes may differ. Check Windows Settings > Personalization > Colors. Windows error: {error}.", L"Les modes du système et des applications peuvent différer. Vérifiez Paramètres > Personnalisation > Couleurs. Erreur Windows : {error}.", L"시스템과 앱 모드가 다를 수 있어요. Windows 설정 > 개인 설정 > 색을 확인하세요. Windows 오류: {error}.") \
 X(ThemeNotifyBody, L"The preference was saved. Some apps may need to be reopened. Windows error: {error}.", L"La préférence a été enregistrée. Certaines applications doivent être rouvertes. Erreur Windows : {error}.", L"설정은 저장됐어요. 일부 앱은 다시 열어야 할 수 있어요. Windows 오류: {error}.") \
 X(ThemeSaveBody, L"Windows could not save the preference. Check Windows Settings > Personalization > Colors. Windows error: {error}.", L"Windows n’a pas pu enregistrer la préférence. Vérifiez Paramètres > Personnalisation > Couleurs. Erreur Windows : {error}.", L"Windows에서 설정을 저장하지 못했어요. 설정 > 개인 설정 > 색을 확인하세요. Windows 오류: {error}.") \
 X(DateSaveError, L"date setting could not be saved", L"préférence de date non enregistrée", L"날짜 설정을 저장하지 못했어요") \
 X(DateSaveBody, L"Windows denied saving the date preference. Your previous choice is unchanged. Windows error: {error}.", L"Windows a refusé d’enregistrer la préférence de date. Votre choix précédent est conservé. Erreur Windows : {error}.", L"Windows에서 날짜 설정 저장을 거부했어요. 이전 설정은 유지돼요. Windows 오류: {error}.") \
 X(ImportError, L"cursor zip could not be imported", L"impossible d’importer le ZIP des curseurs", L"커서 ZIP을 가져오지 못했어요") \
 X(ImportErrorBody, L"Use the original, complete Cursor Concept 3 HDPi ZIP. Choose colors > unlock for instructions. Your current cursor is unchanged.", L"Utilisez le ZIP original et complet de Cursor Concept 3 HDPi. Choisissez couleurs > débloquer pour les instructions. Votre curseur actuel est conservé.", L"Cursor Concept 3 HDPi의 원본 전체 ZIP을 사용하세요. 색상 > 잠금 해제에서 안내를 확인하세요. 현재 커서는 유지돼요.") \
 X(ImportReady, L"cursor colors unlocked", L"couleurs du curseur débloquées", L"커서 색상 잠금이 해제됐어요") \
 X(ImportReadyBody, L"Your pack is ready. Open cursor to choose a color or follow Windows light/dark mode.", L"Votre pack est prêt. Ouvrez curseur pour choisir une couleur ou suivre le mode clair/sombre de Windows.", L"커서 팩이 준비됐어요. 커서 메뉴에서 색상을 선택하거나 Windows 라이트/다크 모드를 따르세요.") \
 X(GuideError, L"cursor guide could not be opened", L"impossible d’ouvrir le guide des curseurs", L"커서 안내를 열지 못했어요") \
 X(GuideErrorBody, L"The cursor files folder could not be created or opened. Check your account’s folder permissions.", L"Impossible de créer ou d’ouvrir le dossier des curseurs. Vérifiez les autorisations du dossier de votre compte.", L"커서 폴더를 만들거나 열지 못했어요. 계정의 폴더 권한을 확인하세요.") \
 X(ShortcutDescription, L"jellyflower taskbar utility", L"jellyflower — utilitaire de barre des tâches", L"jellyflower 작업 표시줄 도구") \
 X(StartupDescription, L"jellyflower: apply the taskbar appearance at sign-in", L"jellyflower : appliquer l’apparence de la barre des tâches à la connexion", L"jellyflower: 로그인 시 작업 표시줄 모양 적용") \
 X(SetupPreparing, L"preparing setup", L"la préparation de l’installation", L"설치 준비") \
 X(SetupUninstalling, L"uninstalling jellyflower", L"la désinstallation de jellyflower", L"jellyflower 제거") \
 X(SetupArchitecture, L"checking Windows architecture", L"la vérification de l’architecture Windows", L"Windows 아키텍처 확인") \
 X(SetupExtracting, L"extracting the new version", L"l’extraction de la nouvelle version", L"새 버전 압축 해제") \
 X(SetupClosing, L"closing the previous version", L"la fermeture de la version précédente", L"이전 버전 종료") \
 X(SetupStartup, L"updating login startup", L"la mise à jour du démarrage à la connexion", L"로그인 시 자동 실행 업데이트") \
 X(SetupRegistering, L"registering the new version", L"l’enregistrement de la nouvelle version", L"새 버전 등록") \
 X(SetupStarting, L"starting the new version", L"le lancement de la nouvelle version", L"새 버전 실행") \
 X(SetupError, L"jellyflower could not finish {step}. Windows error {error}.", L"jellyflower n’a pas pu terminer {step}. Erreur Windows : {error}.", L"jellyflower에서 {step} 단계를 완료하지 못했어요. Windows 오류: {error}.") \
 X(SetupWrongArchitecture, L" Download the installer matching your Windows system type (x64 or ARM64).", L" Téléchargez le programme d’installation adapté à votre système Windows (x64 ou ARM64).", L" Windows 시스템 종류(x64 또는 ARM64)에 맞는 설치 파일을 다운로드하세요.") \
 X(SetupBusy, L" Another installation is still working. Wait for it to finish and try again.", L" Une autre installation est en cours. Attendez qu’elle se termine, puis réessayez.", L" 다른 설치가 진행 중이에요. 완료될 때까지 기다린 후 다시 시도하세요.") \
 X(SetupRetained, L" Your previous installation was retained. Try again, or include this error and step in a GitHub issue.", L" Votre installation précédente a été conservée. Réessayez ou indiquez cette erreur et cette étape dans un ticket GitHub.", L" 이전 설치는 유지됐어요. 다시 시도하거나 GitHub 이슈에 오류와 단계를 알려주세요.") \
 X(SetupRemoved, L"jellyflower has been removed from login startup and your taskbar has been restored.", L"jellyflower ne se lance plus à la connexion et votre barre des tâches a été restaurée.", L"jellyflower의 로그인 시 자동 실행이 해제됐고 작업 표시줄이 복원됐어요.") \
 X(SetupInstalled, L"jellyflower is installed and will start when you sign in.\n\nOpen its flower icon under show hidden icons to use the menu.", L"jellyflower est installé et se lancera à votre connexion.\n\nOuvrez son icône en forme de fleur dans les icônes masquées pour accéder au menu.", L"jellyflower가 설치됐고 로그인하면 자동으로 실행돼요.\n\n숨겨진 아이콘의 꽃 모양 아이콘을 눌러 메뉴를 여세요.") \
 X(SetupReload, L"\n\nSign out and back in once to finish this update.", L"\n\nDéconnectez-vous puis reconnectez-vous pour terminer cette mise à jour.", L"\n\n업데이트를 완료하려면 로그아웃한 후 다시 로그인하세요.") \
 X(GuideIntro, L"jellyflower - unlock cursor colors\r\n\r\nCursor Concept 3 HDPi is made by JepriCreations. Get your own full pack here:\r\n", L"jellyflower - débloquer les couleurs du curseur\r\n\r\nCursor Concept 3 HDPi est créé par JepriCreations. Obtenez votre propre pack complet ici :\r\n", L"jellyflower - 커서 색상 잠금 해제\r\n\r\nCursor Concept 3 HDPi는 JepriCreations의 작품이에요. 여기에서 전체 팩을 받으세요:\r\n") \
 X(GuideBeforeFolder, L"1. Download the complete Cursor Concept 3 HDPi ZIP from your account on the creator’s website.\r\n2. Copy the original .zip file into this folder (also opened for you):\r\n\r\n", L"1. Téléchargez le ZIP complet de Cursor Concept 3 HDPi depuis votre compte sur le site du créateur.\r\n2. Copiez le fichier .zip original dans ce dossier (également ouvert pour vous) :\r\n\r\n", L"1. 제작자 웹사이트의 계정에서 Cursor Concept 3 HDPi 전체 ZIP을 다운로드하세요.\r\n2. 원본 .zip 파일을 아래 폴더에 복사하세요. 폴더도 함께 열려요:\r\n\r\n") \
 X(GuideAfterFolder, L"   Paste that path into File Explorer’s address bar. Keep the file zipped; any .zip filename is fine.\r\n3. Leave jellyflower running. Within ten seconds it validates the pack and enables the cursor choices.\r\n4. Open jellyflower > cursor and pick a color. Default (system) follows Windows light/dark mode.\r\n\r\nYour Windows cursor stays active until you choose a custom cursor. Disable selects Microsoft’s standard cursors.\r\nThe full pack includes light, dark, blueberry, green tea, mint, strawberry, thai tea, vanilla, and yogurt.\r\n", L"   Collez ce chemin dans la barre d’adresse de l’Explorateur de fichiers. Gardez le fichier compressé ; son nom importe peu.\r\n3. Laissez jellyflower ouvert. Il vérifie le pack et active les choix de curseurs en dix secondes environ.\r\n4. Ouvrez jellyflower > curseur et choisissez une couleur. Par défaut (système) suit le mode clair/sombre de Windows.\r\n\r\nVotre curseur Windows reste actif jusqu’à ce que vous en choisissiez un autre. Désactiver rétablit les curseurs standard de Microsoft.\r\nLe pack complet comprend clair, sombre, myrtille, thé vert, menthe, fraise, thé thaï, vanille et yaourt.\r\n", L"   파일 탐색기 주소 표시줄에 경로를 붙여넣으세요. 압축을 풀지 마세요. .zip 파일 이름은 상관없어요.\r\n3. jellyflower를 실행해 두세요. 약 10초 안에 팩을 확인하고 커서 선택을 활성화해요.\r\n4. jellyflower > 커서에서 색상을 선택하세요. 기본값 (시스템)은 Windows 라이트/다크 모드를 따라요.\r\n\r\n다른 커서를 선택하기 전까지는 Windows 커서가 유지돼요. 사용 안 함을 선택하면 Microsoft 기본 커서로 돌아가요.\r\n전체 팩에는 라이트, 다크, 블루베리, 녹차, 민트, 딸기, 타이 티, 바닐라, 요거트가 포함돼요.\r\n") \
 X(GuideDetails, L"The free light/dark ZIP is a different pack and will not unlock these choices. Use the full HDPi ZIP linked above.\r\nNo need to extract files, run install.cmd, or install an INF file. Jellyflower reads only the cursor files.\r\nThe ZIP and imported cursors stay on your computer, survive jellyflower updates, and are never uploaded.\r\n\r\nIf import fails, check the original ZIP contains cursor/assets/<color>/arrow.cur and the other cursor files.\r\nReplace an incomplete download with a fresh ZIP. A failed import keeps your existing pack and cursor.\r\n\r\n", L"Le ZIP gratuit clair/sombre est un autre pack et ne débloque pas ces choix. Utilisez le ZIP HDPi complet indiqué ci-dessus.\r\nNe décompressez rien, ne lancez pas install.cmd et n’installez pas de fichier INF. Jellyflower lit uniquement les fichiers de curseurs.\r\nLe ZIP et les curseurs importés restent sur votre ordinateur, sont conservés lors des mises à jour et ne sont jamais envoyés en ligne.\r\n\r\nEn cas d’échec, vérifiez que le ZIP original contient cursor/assets/<color>/arrow.cur et les autres fichiers de curseurs.\r\nRemplacez un téléchargement incomplet par un nouveau ZIP. Un échec conserve votre pack et votre curseur actuels.\r\n\r\n", L"무료 라이트/다크 ZIP은 다른 팩이라 이 색상을 활성화하지 못해요. 위 링크의 전체 HDPi ZIP을 사용하세요.\r\n압축 해제, install.cmd 실행, INF 설치는 필요 없어요. Jellyflower는 커서 파일만 읽어요.\r\nZIP과 가져온 커서는 컴퓨터에 남고 업데이트 후에도 유지되며 외부로 업로드되지 않아요.\r\n\r\n가져오기에 실패하면 원본 ZIP에 cursor/assets/<color>/arrow.cur와 다른 커서 파일이 있는지 확인하세요.\r\n다운로드가 불완전하면 새 ZIP으로 바꾸세요. 실패해도 기존 팩과 커서는 유지돼요.\r\n\r\n") \
 X(GuideLicense, L"Creator: https://jepricreations.com  |  https://www.deviantart.com/jepricreations\r\nCursor files are licensed by their creator and are not bundled with jellyflower.\r\n", L"Créateur : https://jepricreations.com  |  https://www.deviantart.com/jepricreations\r\nLes fichiers de curseurs sont sous licence de leur créateur et ne sont pas fournis avec jellyflower.\r\n", L"제작자: https://jepricreations.com  |  https://www.deviantart.com/jepricreations\r\n커서 파일은 제작자의 라이선스를 따르며 jellyflower에 포함되지 않아요.\r\n")

enum class TextId {
#define JF_ENUM(id,en,fr,ko) id,
    JELLYFLOWER_TEXT(JF_ENUM)
#undef JF_ENUM
    Count
};
inline constexpr std::array<std::array<const wchar_t*,3>,static_cast<size_t>(TextId::Count)> Translations{{
#define JF_ROW(id,en,fr,ko) {{en,fr,ko}},
    JELLYFLOWER_TEXT(JF_ROW)
#undef JF_ROW
}};
#undef JELLYFLOWER_TEXT

inline bool MatchLanguage(std::wstring_view tag, Language& language) noexcept {
    if(tag.size()<2 || (tag.size()>2 && tag[2]!=L'-'))return false;
    const auto lower=[](wchar_t c){return c>=L'A' && c<=L'Z'?c+(L'a'-L'A'):c;};
    const auto a=lower(tag[0]),b=lower(tag[1]);
    if(a==L'e' && b==L'n')language=Language::English;
    else if(a==L'f' && b==L'r')language=Language::French;
    else if(a==L'k' && b==L'o')language=Language::Korean;
    else return false;
    return true;
}
inline Language SelectLanguage(std::wstring_view multiString) noexcept {
    size_t offset=0;
    while(offset<multiString.size() && multiString[offset]){
        const auto end=multiString.find(L'\0',offset);
        Language result{};
        if(MatchLanguage(multiString.substr(offset,end==std::wstring_view::npos?end:end-offset),result))return result;
        if(end==std::wstring_view::npos)break;
        offset=end+1;
    }
    return Language::English;
}
inline Language DetectLanguage() noexcept {
    try {
        ULONG count=0,size=0;
        if(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,nullptr,&size) && size>1 && size<=4096){
            std::vector<wchar_t> names(size);
            if(GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&count,names.data(),&size))
                return SelectLanguage({names.data(),size});
        }
    }catch(...){}
    const auto primary=PRIMARYLANGID(GetUserDefaultUILanguage());
    return primary==LANG_FRENCH?Language::French:primary==LANG_KOREAN?Language::Korean:Language::English;
}
inline std::atomic<Language>& LanguageCache() {static std::atomic<Language> language{DetectLanguage()};return language;}
inline Language GetLanguage() noexcept {return LanguageCache().load(std::memory_order_relaxed);}
inline void SetTextLanguage(Language language) noexcept {
    LanguageCache().store(language<Language::Count?language:Language::English,std::memory_order_relaxed);
}
inline bool RefreshLanguage() noexcept {const auto next=DetectLanguage();return LanguageCache().exchange(next,std::memory_order_relaxed)!=next;}
inline const wchar_t* Tr(TextId id,Language language=GetLanguage()) noexcept {
    const auto index=static_cast<size_t>(id);
    return index<Translations.size()?Translations[index][language<Language::Count?static_cast<size_t>(language):0]:L"";
}
// Named placeholders allow Korean word order without printf argument risks.
// Substitute once, so braces inside a user's name/path remain literal.
inline std::wstring Text(TextId id,std::initializer_list<std::pair<std::wstring_view,std::wstring_view>> values={},Language language=GetLanguage()) {
    std::wstring result;std::wstring_view source=Tr(id,language);
    while(!source.empty()){
        const auto start=source.find(L'{');
        if(start==std::wstring_view::npos){result+=source;break;}
        result+=source.substr(0,start);source.remove_prefix(start);
        const auto end=source.find(L'}');
        if(end==std::wstring_view::npos){result+=source;break;}
        const auto key=source.substr(1,end-1);bool found=false;
        for(const auto& value:values)if(value.first==key){result+=value.second;found=true;break;}
        if(!found)result+=source.substr(0,end+1);
        source.remove_prefix(end+1);
    }
    return result;
}
inline const wchar_t* OnOff(bool enabled) noexcept {return Tr(enabled?TextId::On:TextId::Off);}
inline const wchar_t* CursorColorLabel(size_t color) noexcept {
    constexpr TextId labels[]{TextId::Light,TextId::Dark,TextId::Blueberry,TextId::GreenTea,TextId::Mint,TextId::Strawberry,TextId::ThaiTea,TextId::Vanilla,TextId::Yogurt};
    return color<9?Tr(labels[color]):L"";
}
inline std::wstring CursorGuideText(std::wstring_view directory,Language language=GetLanguage()) {
    return std::wstring(Tr(TextId::GuideIntro,language))+L"https://jepricreations.com/products/cursor-concept-3-hdpi\r\n\r\n"
        +Tr(TextId::GuideBeforeFolder,language)+std::wstring(directory)+L"\r\n\r\n"+Tr(TextId::GuideAfterFolder,language)
        +Tr(TextId::GuideDetails,language)+Tr(TextId::GuideLicense,language);
}
} // namespace acrylic
