#define ITU_NO_MAIN
#include "src/main.cpp"
#include <cstdlib>
#include "test_helpers.hpp"
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        const auto mixed = json::parse(R"({"courses":{"crn":["001","002"],"scrn":["003"]}})");
        check(registration_payload(mixed).dump() == R"({"ECRN":["001","002"],"SCRN":["003"]})", "payload string/order preservation");
        check(registration_payload(json::parse(R"({"courses":{"crn":[]}})"))["SCRN"].empty(), "missing SCRN");
        check(registration_payload(json::parse(R"({"courses":{"crn":[],"scrn":["004"]}})"))["SCRN"][0] == "004", "drop only");
        check(get_result_message("successResult", "123").find("123") != std::string::npos, "known code");
        check(get_result_message("newCode", "123") == "Bilinmeyen Kod: newCode (CRN 123)", "unknown code");
        std::map<std::string,std::string> values{{"ITU_UNIT_PRIMARY", "file-primary"}, {"ITU_UNIT_LEGACY", "file-legacy"}};
        test_helpers::environment("ITU_UNIT_PRIMARY", nullptr); test_helpers::environment("ITU_UNIT_LEGACY", nullptr);
        test_helpers::environment("ITU_UNIT_LEGACY", "env-legacy");
        check(get_secret_value(values, "ITU_UNIT_PRIMARY", "ITU_UNIT_LEGACY") == "file-primary", "primary file priority");
        test_helpers::environment("ITU_UNIT_PRIMARY", "env-primary");
        check(get_secret_value(values, "ITU_UNIT_PRIMARY", "ITU_UNIT_LEGACY") == "env-primary", "primary process priority");
        test_helpers::environment("ITU_UNIT_PRIMARY", nullptr); values.erase("ITU_UNIT_PRIMARY");
        check(get_secret_value(values, "ITU_UNIT_PRIMARY", "ITU_UNIT_LEGACY") == "env-legacy", "legacy process priority");
        test_helpers::environment("ITU_UNIT_LEGACY", nullptr);
        check(get_secret_value(values, "ITU_UNIT_PRIMARY", "ITU_UNIT_LEGACY") == "file-legacy", "legacy file fallback");
        test_helpers::environment("ITU_UNIT_PRIMARY", u8"üser-東京");
        check(get_secret_value(values,"ITU_UNIT_PRIMARY","ITU_UNIT_LEGACY") == u8"üser-東京", "Unicode process environment");
        test_helpers::environment("ITU_UNIT_PRIMARY", nullptr);
        check(json_int_with_alias(json{{"milisecond",42}}, "millisecond", "milisecond", 0)==42, "time alias");
        // The real .env loader must strip only the outer quote pair: literal
        // inner quotes, whitespace, backslashes and #/= are credential bytes.
        test_helpers::TemporaryDirectory env_directory;
        const auto env_path = env_directory.root / "roundtrip.env";
        const std::string literal = "  both'\"quotes\\ #=literal  ";
        {
            std::ofstream env_output(env_path);
            env_output << "# ignored\nOTHER=retained\nITU_PASSWORD=old\n"
                       << "export ITU_PASSWORD=\"" << literal << "\"\n";
            check(static_cast<bool>(env_output), "write env roundtrip fixture");
        }
        const auto loaded = load_env_file(env_path.u8string());
        std::filesystem::remove(env_path);
        check(loaded.at("ITU_PASSWORD") == literal, "literal env value roundtrip and last duplicate wins");
        check(loaded.at("OTHER") == "retained", "unrelated env value retained");
        using W = SystemClock::Wall; using S = SystemClock::Steady;
        const auto wall = W::from_time_t(100);
        const auto steady = S::time_point(std::chrono::seconds(50));
        const auto deadline = SystemClock::deadline(wall+std::chrono::seconds(5), wall, steady, 200, 50);
        check(deadline-steady == std::chrono::milliseconds(4750), "offset and lead");
        check(SystemClock::deadline(wall,wall,steady,0,-10)==steady, "negative lead clamped");
        check(SystemClock::deadline(wall-std::chrono::seconds(1),wall,steady,0,0)<steady, "past target");
        // Once computed, a monotonic deadline does not read later wall-clock changes.
        check(deadline-(steady+std::chrono::seconds(2))==std::chrono::milliseconds(2750), "monotonic countdown");
        test_helpers::eastern_timezone();
        const auto summer=SystemClock::local_target(2026,7,1,12,0,0,125);
        const auto epoch=W::to_time_t(summer);
        std::tm utc = test_helpers::utc(epoch);
        check(utc.tm_hour==16, "summer DST detected");
        check(std::chrono::duration_cast<std::chrono::milliseconds>(summer-W::from_time_t(epoch)).count()==125,"milliseconds");
        const auto winter=W::to_time_t(SystemClock::local_target(2026,1,1,12,0));
        utc = test_helpers::utc(winter); check(utc.tm_hour==17,"winter standard time");
        SystemClock clock;
        int attempts=0, sleeps=0, ticks=0;
        const int rtts[]{80,20,20,40,60,70,90};
        clock.sample({[&](const HttpRequest& req){
            check(req.method=="HEAD" && req.timeout_ms==5000,"sampling request contract");
            ++attempts;
            return HttpResponse{200, "", {{"date",attempts==3?"Thu, 01 Jan 1970 00:01:41 GMT":"Thu, 01 Jan 1970 00:01:40 GMT"}}, ""};
        },[&]{return wall;},[&]{ const int index=ticks/2; const bool end=ticks++%2; return steady+std::chrono::milliseconds(end?rtts[index]:0); },
        [&](std::chrono::milliseconds delay){check(delay.count()==300,"sample spacing");++sleeps;}},"https://fixture.invalid");
        check(attempts==7 && sleeps==6,"seven attempts six intervals");
        check(clock.get_offset()==490,"lowest RTT first tie selection");
        attempts=0;
        clock.sample({[&](const HttpRequest&){++attempts; return HttpResponse{200,"",{{"date","invalid"}},""};},
                     [&]{return wall;},[&]{return steady;},[](auto){}},"https://fixture.invalid");
        check(attempts==7 && clock.get_offset()==0,"invalid date fallback resets offset");
        attempts=0;
        clock.sample({[&](const HttpRequest&)->HttpResponse{++attempts; throw std::runtime_error("synthetic failure");},
                     [&]{return wall;},[&]{return steady;},[](auto){}},"https://fixture.invalid");
        check(attempts==7 && clock.get_offset()==0,"transport failure fallback");
        // Run the real wait loop while the injected wall clock jumps in either direction.
        for (const int jump : {-3600, 3600}) {
            auto wall_sim = SystemClock::local_target(2026,7,1,12,0,0);
            auto steady_sim = steady;
            int wall_reads = 0, spins = 0, sleeps_count = 0;
            clock.wait_until_with({[&]{++wall_reads; return wall_sim;}, [&]{return steady_sim;},
                [&](std::chrono::milliseconds delay){
                    steady_sim += delay; wall_sim += std::chrono::seconds(jump); ++sleeps_count;
                }, [&]{steady_sim += std::chrono::milliseconds(1); ++spins;}},
                2026,7,1,12,0,3,0,0);
            check(wall_reads==1 && sleeps_count>0 && spins>0,"wait loop uses one wall-clock snapshot");
            check(steady_sim-steady==std::chrono::seconds(3),"wall jumps do not move steady deadline");
        }
        clock.wait_until(2000,1,1,0,0);
        {
            itu::platform::TimingGuard guard;
            guard.activate();
            guard.activate();
        }
        std::cout << "Core and clock tests passed.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
