
#include <iostream>
#include "particles.h"
#include "state.h"
#include "particle.h"
#include <vector>
#include "move.h"
#include "simulator.h"

int main(){
    //ps.push_back(new RPM());
    //ps.push_back(new ARPM());
    //trans.operator()<decltype(ps[1])>(ps[0]);

    Simulator* sim = new Simulator(10, 10);
    sim->run();
    sim->state.set_geometry(0);
    sim->state.set_energy(0);
    sim->state.load_particles();
    std::vector< std::vector<double> > pos;
    pos.push_back(new std::vector<double>);
    pos.back = {1, 2, 3}
    sim->run(pos, 1, 1);
    //std::function<void(std::vector<int>)> move_callback = [currentState](std::vector<int> indices) { currentState.move_callback(indices); }
}