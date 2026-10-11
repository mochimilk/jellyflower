// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/solar.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace acrylic;
void Check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
SYSTEMTIME Time(WORD month, WORD day, WORD hour, WORD minute = 0) { SYSTEMTIME time{}; time.wYear=2026; time.wMonth=month; time.wDay=day; time.wHour=hour; time.wMinute=minute; return time; }
int main() {
    try {
        Check(!SunIsUp(Time(10,10,13,45),34.05,-118.24), "Los Angeles before sunrise");
        Check(SunIsUp(Time(10,10,14,15),34.05,-118.24), "Los Angeles after sunrise");
        Check(SunIsUp(Time(10,11,1,10),34.05,-118.24), "Los Angeles before sunset across UTC midnight");
        Check(!SunIsUp(Time(10,11,1,45),34.05,-118.24), "Los Angeles after sunset");
        Check(SunIsUp(Time(3,20,3),35.68,139.69), "Tokyo daytime east longitude");
        Check(!SunIsUp(Time(3,20,15),35.68,139.69), "Tokyo night");
        Check(SunIsUp(Time(6,21,0),80,0) && SunIsUp(Time(6,21,12),80,0), "polar summer all day");
        Check(!SunIsUp(Time(12,21,0),80,0) && !SunIsUp(Time(12,21,12),80,0), "polar winter all night");
        Check(SunIsUp(Time(12,21,0),-80,0), "southern hemisphere reversed seasons");
        Check(SunIsUp(Time(3,20,0),0,180) == SunIsUp(Time(3,20,0),0,-180), "date line equivalent longitude");
        Check(!ValidCoordinates(std::numeric_limits<double>::quiet_NaN(),0) && !ValidCoordinates(91,0) && !ValidCoordinates(0,181), "invalid location rejected");
        auto leap=Time(2,29,12);leap.wYear=2028; Check(SunIsUp(leap,0,0), "leap date");
        const auto now=SolarSeconds(Time(10,10,12));
        SolarSchedule schedule;Check(schedule.Update(now,34.05,-118.24),"first calculation builds a day of events");
        const auto until=schedule.Until(), sunrise=schedule.Next(now);
        Check(until==now+86400 && sunrise>SolarSeconds(Time(10,10,13,45)) && sunrise<SolarSeconds(Time(10,10,14,15)),"first deadline is sunrise, not a polling interval");
        Check(!schedule.Light(sunrise-1) && schedule.Light(sunrise),"sunrise changes at its cached one-second boundary");
        const auto sunset=schedule.Next(sunrise);
        Check(sunset>SolarSeconds(Time(10,11,1,10)) && sunset<SolarSeconds(Time(10,11,1,45)),"sunset deadline crosses UTC midnight");
        Check(schedule.Light(sunset-1) && !schedule.Light(sunset) && schedule.Next(sunset)==until,"sunset consumes only its event then waits for daily refresh");
        for(auto t=now;t<until;t+=5)Check(!schedule.Update(t,34.05,-118.24),"ordinary refreshes reuse the same calculated day");
        Check(schedule.Update(until,34.05,-118.24) && schedule.Until()==until+86400,"one rebuild when the day expires");
        Check(schedule.Update(now,34.05,-118.24),"backward clock adjustment rebuilds");
        Check(schedule.Update(now+2*86400,34.05,-118.24),"wake after missed events rebuilds and catches up");
        Check(schedule.Update(now+2*86400,35.68,139.69),"new location invalidates old event times");
        schedule.Invalidate();Check(schedule.Update(now+2*86400,35.68,139.69),"explicit clock/resume invalidation rebuilds even inside a cached day");
        for(const auto month:{1,3,6,10,12}) for(const auto lat:{-80.0,-67.4,-34.0,0.0,34.05,67.4,80.0}) for(const auto lon:{-180.0,-118.24,0.0,139.69,180.0}) {
            const auto first=SolarSeconds(Time(static_cast<WORD>(month),21,0));
            schedule.Update(first,lat,lon);
            for(auto t=first;t<first+86400;t+=60)Check(schedule.Light(t)==SunIsUp(SolarTime(t),lat,lon),"cached day agrees with direct solar position across hemispheres/polar/date line");
        }
        const auto summer=SolarSeconds(Time(6,21,0));schedule.Update(summer,80,0);
        Check(schedule.Light(summer) && schedule.Next(summer)==summer+86400,"polar day waits until next daily refresh");
        const auto winter=SolarSeconds(Time(12,21,0));schedule.Update(winter,80,0);
        Check(!schedule.Light(winter) && schedule.Next(winter)==winter+86400,"polar night needs no fake sunrise timer");
        auto left=winter+10*3600,right=winter+14*3600;
        while(right-left>3){const auto a=left+(right-left)/3,b=right-(right-left)/3;if(SolarElevation(SolarTime(a),67.4,0)<SolarElevation(SolarTime(b),67.4,0))left=a;else right=b;}
        const auto noon=left+(right-left)/2;double low=66,high=69;
        for(unsigned i=0;i<40;++i){const auto latitude=(low+high)/2;if(SunIsUp(SolarTime(noon),latitude,0))low=latitude;else high=latitude;}
        const auto shortDayLatitude=low-.0001;schedule.Update(winter+13,shortDayLatitude,0);
        const auto shortRise=schedule.Next(winter+13),shortSet=schedule.Next(shortRise);
        Check(shortRise<noon && shortSet>noon && shortSet-shortRise<300,"extremum refinement finds a polar day shorter than the five-minute calculation samples");
        for(auto t=noon-180;t<noon+180;++t)Check(schedule.Light(t)==SunIsUp(SolarTime(t),shortDayLatitude,0),"short polar transitions match the direct model to one second");
        const auto feb=SolarSeconds(leap);schedule.Update(feb,0,0);
        Check(schedule.Light(feb+3600)==SunIsUp(SolarTime(feb+3600),0,0),"leap day schedule uses real UTC dates");
        std::cout<<"PASS: solar boundaries, cached daily sunrise/sunset deadlines, no five-second recalculation, clock/wake/location invalidation, hemispheres/date line/polar/leap cases.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
