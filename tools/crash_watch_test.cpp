// tools/crash_watch_test.cpp - shared/d2r_vr_crash_watch.h, the contract with D2R VR Crash Reporter ([support] crash_reports /
// send_logs, docs/crash_watchdog.md): the quiet event's name the reporter opens, where the plugin and Settings look for the reporter,
// which report folders Settings offers again. And shared/crash_reply.h: our reply to a report the player sent (sent.jsonl, the
// server's answer, replies.jsonl, which reports to ask about, the dismissed list, which reply shows). And shared/problem_report.h: Report a
// problem (the issue's address and issues.jsonl, what was just sent, the player's text, the settings changed from the shipped ones).
#include "crash_reply.h"
#include "d2r_vr_crash_watch.h"
#include "problem_report.h"

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

    // Report a problem (shared/problem_report.h): the issue's address the server gives - only an issue of our public repository
    namespace pr = problemreport;
    Check(pr::ParseIssue("{\"status\":\"none\",\"id\":4210,\"issue\":\"https://github.com/natpoh/diablo2-vr-port/issues/19\"}") ==
              "https://github.com/natpoh/diablo2-vr-port/issues/19", "an issue with a 'none' answer (no reply yet, the issue already there)");
    Check(pr::ParseIssue("{\"status\":\"ok\",\"text\":\"x\",\"issue\":\"https:\\/\\/github.com\\/natpoh\\/diablo2-vr-port\\/issues\\/7\"}") ==
              "https://github.com/natpoh/diablo2-vr-port/issues/7", "an escaped one");
    Check(pr::ParseIssue("{\"status\":\"none\"}").empty() && pr::ParseIssue("{\"issue\":\"\"}").empty(), "none");
    Check(pr::ParseIssue("{\"issue\":\"https://github.com/natpoh/diablo2-vr-port/issues/19/../../evil\"}").empty() &&
              pr::ParseIssue("{\"issue\":\"https://evil.example/natpoh/diablo2-vr-port/issues/19\"}").empty() &&
              pr::ParseIssue("{\"issue\":\"http://github.com/natpoh/diablo2-vr-port/issues/19\"}").empty() &&
              pr::ParseIssue("{\"issue\":\"https://github.com/natpoh/diablo2-vr-port/issues/\"}").empty() &&
              pr::ParseIssue("{\"issue\":\"https://github.com/natpoh/diablo2-vr-port/issues/012\"}").empty() &&
              pr::ParseIssue("{\"issue\":\"https://github.com/natpoh/diablo2-vr-port/issues/12345678\"}").empty() &&
              pr::ParseIssue("{\"issue\":\"https://github.com/natpoh/diablo2-vr-port/issues/19?x=1\"}").empty(),
          "anything else is never opened");
    const std::string issues = pr::IssueLine(4210, "https://github.com/natpoh/diablo2-vr-port/issues/19") + "garbage\r\n" +
                               "{\"id\":4211,\"issue\":\"https://evil.example/1\"}\r\n" + pr::IssueLine(4212, "https://github.com/natpoh/diablo2-vr-port/issues/21");
    const auto im = pr::ParseIssues(issues);
    Check(im.size() == 2 && im.at(4210).ends_with("/19") && pr::NewestIssue(im) == 4212 && pr::NewestIssue({}) == 0, "issues.jsonl written and read back");

    const std::string before = "{\"id\":4201,\"token\":\"mockReplyToken_4201_abcdefgh\",\"kind\":\"crash\"}\r\n";
    const auto fresh = pr::NewlySent(before, before + "{\"id\":4210,\"token\":\"mockReplyToken_4210_abcdefgh\",\"kind\":\"logs\"}\r\n");
    Check(fresh.size() == 1 && fresh[0].id == 4210 && pr::NewlySent(before, before).empty() && pr::NewlySent("", before).size() == 1,
          "what the player just sent: the new lines of sent.jsonl");

    Check(pr::CleanText("  the fog doesn't work \r\n") == "the fog doesn't work" && pr::CleanText(" ?!  ...  ").empty() && pr::CleanText("abc").empty(),
          "the description: trimmed; nothing said, nothing sent");
    Check(pr::CleanText("\xD0\xBD\xD0\xB5\xD1\x82 \xD1\x82\xD1\x83\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB0") ==
              "\xD0\xBD\xD0\xB5\xD1\x82 \xD1\x82\xD1\x83\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB0", "another script's letters count");
    const std::string longText = std::string(3999, 'a') + "\xD0\xB6" + "tail";
    Check(pr::CleanText(longText).size() == 3999, "cut at 4000 bytes on a character's start");

    const std::string shipped = "\xEF\xBB\xBF; head\r\nbackground_full_speed=1\r\n[fog]\r\n; c\r\ngame_fog_before_fx=1\r\nfar=40\r\n[ceiling]\r\ngame_ceiling=1\r\n";
    const std::string mine = "background_full_speed=1\r\n[Fog]\r\nGame_Fog_Before_FX = 0\r\nfar=40\r\nfar=99\r\n[ceiling]\r\ngame_ceiling=1\r\n[debug]\r\nx=1\r\n";
    const std::string diff = pr::IniDiffText(mine, shipped);
    Check(diff.starts_with("Settings changed from the shipped d2r_vr.ini: 2\r\n") &&
              diff.find("[Fog] Game_Fog_Before_FX = 0   (shipped: 1)") != std::string::npos &&
              diff.find("[debug] x = 1   (not in the shipped file)") != std::string::npos && diff.find("far") == std::string::npos &&
              diff.find("game_ceiling") == std::string::npos && diff.find("background") == std::string::npos,
          "settings changed: case-insensitive, the first value of a key counts, comments and BOM skipped");
    Check(pr::IniDiffText(shipped, shipped).starts_with("Settings changed from the shipped d2r_vr.ini: 0"), "nothing changed");

    // problem.txt: the header the server reads for the public issue (only fixed values), then an empty line, then the player's words
    const std::string pt = pr::ProblemText("0.211.0", "2026-11-10 12:00", 3, 0, 1, "  no fog\nView: F1\r\nHeadset: Valve Index  ");
    Check(pt == "Problem described by the player in D2R VR Settings 0.211.0, 2026-11-10 12:00 (the player's clock)\r\nView: F4\r\n"
                "Headset: Meta Quest 3\r\nGame running: yes\r\n\r\nno fog\r\nView: F1\r\nHeadset: Valve Index\r\n",
          "problem.txt: view F4, Quest 3, game running in the header; the words after the empty line, line ends CRLF");
    const std::string pn = pr::ProblemText("0.211.0", "2026-11-10 12:00", -1, 99, -1, "the ceilings are too low");
    Check(pn.find("View:") == std::string::npos && pn.find("Headset:") == std::string::npos && pn.find("Game running") == std::string::npos &&
              pn.find("(the player's clock)\r\n\r\nthe ceilings are too low\r\n") != std::string::npos,
          "not sure / out of the list / not known: no line");
    Check(std::string(pr::kViews[3]) == "F4" && pr::kViewCount == 5 && pr::kHeadsetCount == 12 && std::string(pr::kHeadsets[11]) == "Other",
          "the lists the server knows (services/github_issues.py VIEWS / HEADSETS)");

    std::printf("crash_watch_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
