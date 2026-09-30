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
        json trace = {{"events", json::array()}, {"resync_seconds", 0},
                      {"token_waits", 0}, {"final_waits", 0}};
        if (future) {
            schedule.wall_now = [&] { return fake_now; };
            schedule.sleep_for = [&](std::chrono::seconds duration) {
                if (trace["resync_seconds"].get<int>() == 0)
                    trace["events"].push_back("resync_wait");
                trace["resync_seconds"] = trace["resync_seconds"].get<int>() + duration.count();
                fake_now += duration;
            };
            schedule.sleep_until = [&](auto deadline) {
                trace["events"].push_back("token_wait");
                trace["token_waits"] = trace["token_waits"].get<int>() + 1;
                trace["token_deadline"] = std::chrono::duration_cast<std::chrono::seconds>(deadline - target).count();
                if (deadline > fake_now) fake_now = deadline;
            };
            schedule.final_wait = [&](SystemClock&, int year, int month, int day, int hour,
                                      int minute, int second, int ms, int lead, TimingQoS* qos) {
                if (!qos) throw std::runtime_error("missing QoS guard");
                trace["events"].push_back("final_wait");
                trace["final_waits"] = trace["final_waits"].get<int>() + 1;
                trace["final_target"] = {year, month, day, hour, minute, second, ms, lead};
            };
        }
        int result = run_application(argc-1,argv+1,session,
            [&](const std::string& user,const std::string& pass,bool){
                if(user!="fixture-user" || pass!="fixture-password") throw std::runtime_error("credentials");
                if (future) {
                    trace["events"].push_back("acquire_token");
                    trace["token_time"] = std::chrono::duration_cast<std::chrono::seconds>(fake_now - target).count();
                }
                return "Bearer fixture.header.signature";
            },argv[1],schedule);
        if (future) std::cout << "FIXTURE_SCHEDULE=" << trace.dump() << '\n';
        return result;
    } catch(const std::exception&) {std::cerr<<"[Fatal] Fixture application failed.\n";return 1;}
}
