#define ITU_NO_MAIN
#include "src/main.cpp"
int main(int argc,char* argv[]) {
    if(argc<2) return 2;
    try {
        HttpSession session(HttpSession::TestOptions{true,{}});
        ApplicationSchedule schedule;
        const bool future = std::getenv("ITU_FIXTURE_FUTURE") != nullptr;
        const auto target = SystemClock::local_target(2030, 1, 1, 0, 0);
        auto fake_now = target - std::chrono::seconds(120);
        auto fake_steady = SystemClock::Steady::time_point{};
        std::string stage;
        int health_checks = 0;
        bool corrected_clock = false;
        json trace = {{"events", json::array()}, {"resync_seconds", 0},
                      {"token_waits", 0}, {"final_waits", 0}};
        schedule.clock_health = [&] {
            itu::platform::ClockHealth health;
            health.provider = "synthetic";
            health.reason = "unavailable";
            if (std::getenv("ITU_FIXTURE_UNSYNCHRONIZED") && ++health_checks > 1) health.synchronized = false;
            return health;
        };
        if (future) {
            schedule.wall_now = [&] { return fake_now; };
            schedule.steady_now = [&] { return fake_steady; };
            schedule.sleep_ms = [&](std::chrono::milliseconds duration) {
                fake_now += duration;
                fake_steady += duration;
                if (std::getenv("ITU_FIXTURE_CLOCK_STEP") && stage == "token_wait") {
                    fake_now += std::chrono::seconds(5);
                }
                if (std::getenv("ITU_FIXTURE_SMALL_CORRECTION") && stage == "final_wait" && !corrected_clock) {
                    fake_now -= std::chrono::milliseconds(200);
                    corrected_clock = true;
                }
            };
            schedule.spin = [&] {
                fake_now += std::chrono::milliseconds(1);
                fake_steady += std::chrono::milliseconds(1);
            };
            schedule.waiting = [&](const char* name, auto deadline) {
                stage = name;
                trace["events"].push_back(name);
                if (stage == "resync_wait") trace["resync_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(deadline - fake_now).count();
                if (stage == "token_wait") {
                    trace["token_waits"] = trace["token_waits"].get<int>() + 1;
                    trace["token_deadline"] = std::chrono::duration_cast<std::chrono::seconds>(deadline - target).count();
                }
                if (stage == "final_wait") {
                    trace["final_waits"] = trace["final_waits"].get<int>() + 1;
                    trace["final_deadline_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - target).count();
                }
            };
            schedule.cancelled = [&] { return std::getenv("ITU_FIXTURE_CANCEL_FINAL") && stage == "final_wait"; };
        }
        TokenProvider provider(
            [&](const std::string& user,const std::string& pass,bool){
                if(user!="fixture-user" || pass!="fixture-password") throw std::runtime_error("credentials");
                if (future) {
                    trace["events"].push_back("acquire_token");
                    trace["token_time"] = std::chrono::duration_cast<std::chrono::seconds>(fake_now - target).count();
                }
                return "Bearer fixture.header.signature";
            });
        std::function<bool()> retained_cancel;
        HttpSession::BeforeTransfer retained_before;
        HttpSession::AfterTransfer retained_after;
        provider.set_cancelled = [&](auto callback) { retained_cancel = std::move(callback); };
        provider.observers = [&](auto before, auto after) { retained_before = std::move(before); retained_after = std::move(after); };
        int result = run_application(argc-1,argv+1,session,provider,argv[1],schedule);
        if (retained_cancel || retained_before || retained_after) throw std::runtime_error("callbacks not reset");
        if (future) {
            trace["finish_wall_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(fake_now - target).count();
            std::cout << "FIXTURE_SCHEDULE=" << trace.dump() << '\n';
        }
        return result;
    } catch(const std::exception&) {std::cerr<<"[Fatal] Fixture application failed.\n";return 1;}
}
