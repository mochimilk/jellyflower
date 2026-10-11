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
        std::cout<<"PASS: sunrise/sunset boundaries, UTC midnight, east/west/date line, polar seasons, leap date and invalid coordinates.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
