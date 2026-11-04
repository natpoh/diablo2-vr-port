// tools/crash_watch_test.cpp - shared/d2r_vr_crash_watch.h, the contract with D2R VR Crash Reporter ([support] crash_reports /
// send_logs, docs/crash_watchdog.md): the quiet event's name the reporter opens, where the plugin and Settings look for the reporter,
// which report folders Settings offers again. And shared/crash_reply.h: our reply to a report the player sent (sent.jsonl, the
// server's answer, replies.jsonl, which reports to ask about, the dismissed list, which reply shows).
#include "crash_reply.h"
#include "d2r_vr_crash_watch.h"

#include <cstdio>

static int g_fail = 0, g_pass = 0;
static void Check(bool ok, const char* what) {
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL: %s\n", what); }
}

int main() {
    Check(crashwatch::QuietName(26840) == L"Local\\D2RVR_Quiet_26840", "the quiet event's name (the reporter opens exactly this)");
    Check(crashwatch::ReporterPath(L"C:\\G\\mods\\D2R-VR\\d2rloader\\plugins\\") ==
              L"C:\\G\\mods\\D2R-VR\\d2rloader\\plugins\\..\\d2r_vr_libs\\D2R_VR_Crash_Reporter.exe",
          "the reporter beside the libraries");
    Check(crashwatch::Pending("pending\r\n") && crashwatch::Pending("pending"), "a pending report is offered again");
    Check(!crashwatch::Pending("sent 4201\r\n") && !crashwatch::Pending("declined\r\n") && !crashwatch::Pending(""),
          "a sent, declined or unknown one never");
    Check(crashwatch::D2RVR_CRASH_ABI == 1 && crashwatch::kExitNotAllowed == 3, "the interface version and the 'not allowed' exit");

    // sent.jsonl as the reporter writes it (a bad line left out)
    const std::string sent =
        "{\"id\":4201,\"token\":\"mockReplyToken_4201_abcdefgh\",\"sent\":\"2026-11-04 20:07:37\",\"kind\":\"crash\",\"folder\":\"20261104-200733\"}\r\n"
        "{\"id\":0,\"token\":\"x\"}\r\n"
        "garbage\r\n"
        "{\"id\":4202,\"token\":\"mockReplyToken_4202_abcdefgh\",\"sent\":\"2026-11-04 20:07:46\",\"kind\":\"logs\",\"folder\":\"20261104-200746-logs\"}\r\n";
    const auto s = crashreply::ParseSent(sent);
    Check(s.size() == 2 && s[0].id == 4201 && s[0].token == "mockReplyToken_4201_abcdefgh" && s[1].kind == "logs", "sent.jsonl read");

    // the server's answers
    crashreply::Reply r;
    Check(crashreply::ParseAnswer("{\"status\":\"none\",\"id\":4201}", &r) == 0, "no reply yet");
    Check(crashreply::ParseAnswer("{\"detail\":\"unknown\"}", &r) == -1, "a 404's body is not an answer");
    const int got = crashreply::ParseAnswer(
        "{\"status\":\"ok\",\"id\":4201,\"text\":\"RTSSHooks64 found: close \\\"RivaTuner\\\"\\nthen start again \\u2014 \\ud83d\\ude00\","
        "\"fixed_in\":\"0.201.1\",\"kind\":\"signature\",\"date\":\"2026-11-04\"}", &r);
    Check(got == 1 && r.text == "RTSSHooks64 found: close \"RivaTuner\"\nthen start again \xE2\x80\x94 \xF0\x9F\x98\x80" && r.fixed_in == "0.201.1" &&
              r.kind == "signature" && r.date == "2026-11-04", "an answer: quotes, a new line, \\u escapes and a surrogate pair undone as UTF-8");
    Check(crashreply::ParseAnswer("{\"status\":\"ok\",\"text\":\"\"}", &r) == -1, "an empty reply is none of ours");

    // replies.jsonl: written and read back the same
    crashreply::Reply a;
    a.id = 4201; a.text = "Thanks - \"fixed\"\nupdate please \\o/"; a.fixed_in = "0.201.1"; a.date = "2026-11-04"; a.kind = "report";
    const auto back = crashreply::ParseReplies(crashreply::ReplyLine(a) + "bad line\r\n");
    Check(back.size() == 1 && back[0].id == 4201 && back[0].text == a.text && back[0].fixed_in == "0.201.1" && back[0].kind == "report",
          "a reply line written and read back unchanged");

    // which to ask about, which to show
    const auto ask = crashreply::ToAsk(s, back);
    Check(ask.size() == 1 && ask[0].id == 4202, "only the reports without an answer are asked about");
    Check(crashreply::ParseDismissed(" 12, 45,x,0,7") == std::set<int>({7, 12, 45}) && crashreply::JoinDismissed({45, 7, 12}) == "7,12,45",
          "the dismissed list read and written");
    crashreply::Reply b = a;
    b.id = 4202;
    const std::vector<crashreply::Reply> both{a, b};
    Check(crashreply::ToShow(both, {}, "0.201.0")->id == 4202, "the newest reply shows first");
    Check(crashreply::ToShow(both, {4202}, "0.201.0")->id == 4201, "a dismissed one gives way to the next");
    Check(crashreply::ToShow(both, {4201, 4202}, "0.201.0") == nullptr, "all dismissed: none");

    // "fixed in 0.202.0 - please update" (Serg: "обновитесь до следующей версии, ваша ошибка исправлена")
    Check(crashreply::VersionLess("0.201.0", "0.202.0") && crashreply::VersionLess("0.202.0", "0.202.1") &&
              crashreply::VersionLess("0.9.9", "0.10.0") && !crashreply::VersionLess("0.202.0", "0.202.0") &&
              !crashreply::VersionLess("0.202.0-develop", "0.202") && crashreply::VersionLess("0.202", "0.202.1"),
          "versions compared number by number");
    crashreply::Reply onlyFix;
    Check(crashreply::ParseAnswer("{\"status\":\"ok\",\"id\":5,\"text\":\"\",\"fixed_in\":\"0.202.0\",\"kind\":\"signature\",\"date\":\"2026-11-05\"}",
                                  &onlyFix) == 1 && onlyFix.text.empty() && onlyFix.fixed_in == "0.202.0", "a version-only answer is a reply");
    onlyFix.id = 5;
    Check(crashreply::HowToShow(onlyFix, "0.201.0") == crashreply::Show::Update, "fixed in a newer version: Update");
    Check(crashreply::HowToShow(onlyFix, "0.202.0") == crashreply::Show::Nothing && crashreply::HowToShow(onlyFix, "0.203.0") == crashreply::Show::Nothing,
          "version-only, and that version is here: nothing to show");
    crashreply::Reply textFix = onlyFix;
    textFix.text = "It was the frame generation.";
    Check(crashreply::HowToShow(textFix, "0.202.1") == crashreply::Show::Text, "words and a version that is here: the words");
    const std::vector<crashreply::Reply> mix{a, onlyFix};
    Check(crashreply::ToShow(mix, {4201}, "0.202.0") == nullptr, "a version-only reply already fixed here is not shown");
    Check(crashreply::AnyUnread(mix, {}, {}, "0.201.0") && !crashreply::AnyUnread(mix, {4201, 5}, {}, "0.201.0") &&
              !crashreply::AnyUnread(mix, {}, {4201}, "0.202.0") && crashreply::AnyUnread(mix, {4201}, {}, "0.201.0"),
          "unread: shown-worthy, not read, not dismissed (the launcher's unread_replies)");

    std::printf("crash_watch_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
