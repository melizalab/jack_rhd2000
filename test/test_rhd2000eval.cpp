
#include <iostream>
#include <iomanip>
#include <boost/shared_ptr.hpp>
#include "rhd2000eval.hpp"

using namespace std;

static const unsigned int sampling_rate = 30000;
boost::shared_ptr<rhd2000eval> dev;


int
main(int, char**)
{
        dev.reset(new rhd2000eval(sampling_rate));
        std::cout << *dev << endl;

}
