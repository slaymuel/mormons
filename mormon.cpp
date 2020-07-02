#define UNUSED(x) (void)(x)

#include <random>
#include "random.h"
#include <assert.h>
#include "constants.h"
#include <iostream>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "aux_math.h"
#include "state.h"
#include "particle.h"
#include "move.h"
#include <functional>
#include <chrono>
#include "sampler.h"
#include <algorithm>
#include "comparators.h"
#include "io.h"
#ifdef PY11
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
#endif



class Simulator{
    
    private:
    std::vector<Particle*> ps;
    std::vector<double> mWeights;
    std::vector<double>::iterator wIt;
    std::vector<Move*> moves;
    std::vector<Sampler*> sampler;

    /* State callback after move */
    //std::function< void(std::vector< unsigned int >) > move_callback 
    //            = std::bind(&State::move_callback, &state, std::placeholders::_1);
    std::string name;

    public:
    State state;
    Simulator(double Dielec, double T, std::string name){
        //Set some constants
        constants::D = Dielec;
        constants::T = T; 
        constants::lB = constants::C * (1.0 / (Dielec * T));
        this->name = name;
        Random::initialize();

        #ifdef _OPENMP
        printf("\nOpenMP is ENABLED with %i threads.\n\n", omp_get_num_procs());
        #else
        printf("\nOpenMP is DISABLED\n");
        #endif

        #ifdef DEBUG
        printf("Debug mode is ENABLED\n");
        #else
        printf("Debug mode is DISABLED\n");
        #endif
        printf("\n");
    }
    
    void set_temperature(double T){
        constants::T = T; 
        constants::lB = constants::C * (1.0 / (constants::D * T));
    }
    
    void set_cp(double cp){
        constants::cp = cp;
    }


    void add_move(int i, double dp, double p, double cp = 0.0, double d = 0.0){
        printf("\nAdding move:\n");
        switch(i){
            case 0:
                moves.push_back(new Translate(dp, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 1:
                moves.push_back(new GrandCanonical<true>(cp, d, &state, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 2:
                moves.push_back(new GrandCanonical<false>(cp, d, &state, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 3:
                moves.push_back(new Rotate(dp, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 4:
                moves.push_back(new Swap(&state, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 5:
                moves.push_back(new SingleSwap(&state, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 6:
                moves.push_back(new VolumeMove(&state, dp, cp, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 7:
                moves.push_back(new ChargeTrans(&state, dp, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            case 8:
                moves.push_back(new ChargeTransRand(&state, dp, p, std::bind(&State::move_callback, &state, std::placeholders::_1)));
                break;
            default:
                printf("Could not find move %i\n", i);
                break;
        }
    }

    void add_sampler(int i, int interval){
        switch(i){
            case 0:
                printf("Adding density sampler\n");
                sampler.push_back(new Samplers::Density(2, this->state.geo->_d[2], 0.05, 
                                              this->state.geo->d[0], this->state.geo->d[1], interval, this->name));
                break;
            case 1:
                printf("Adding Widom HS-CP sampler\n");
                sampler.push_back(new Samplers::WidomHS(interval, this->name));
                break;

            case 2:
                printf("Adding energy sampler\n");
                sampler.push_back(new Samplers::Energy(interval, this->name));
                break;

            case 3:
                printf("Adding charge distribution sampler\n");
                sampler.push_back(new Samplers::QDist(4, 0.05, interval, this->name));
                break;
            case 4:
                printf("Adding XDR trajectory sampler\n");
                sampler.push_back(new Samplers::XDR(interval, this->name));
                break;
            case 5:
                printf("Adding number of ions sampler\n");
                sampler.push_back(new Samplers::NumIons(interval, this->name));
                break;
            default:
                break;
        }
    }

    void finalize(){
        std::for_each( this->moves.begin(), this->moves.end(), [&](Move* m){ this->mWeights.push_back(m->weight); } );
        std::sort(this->moves.begin(), this->moves.end(), comparators::mLess);
        std::sort(this->mWeights.begin(), this->mWeights.end());

        for(unsigned int i = 1; i < this->mWeights.size(); i++){
            this->mWeights[i] += this->mWeights[i - 1];
        }

        //Make sure move list is not corrupted
        assert(this->mWeights.back() == 1.0);

        this->state.finalize(this->name);
    }

    void run(unsigned int macroSteps, unsigned int microSteps, unsigned int eqSteps){

        printf("            +\n"                                            
               "           (|)\n"
               " _____.___.|_|.\n"                                      
               "|    / \\  |===|\n"                                           
               "|   /   \\ | o |               MORMONS\n"                                           
               "|__/__v__\\|, ,|    MOleculaR MOdelliNg Software\n"                                           
               "| | | | | || ||   -----------------------------\n"   
               "|/| . . . |','|\n"                                           
               "||| A A A | , |\n"                                           
               "||| M M M |   |\n"                                       
               "---------------\n\n");


        printf("Bjerrum length is: %.15lf\n", constants::lB);
        std::cout << "Running simulation at: " << constants::T << "K, "
                                                                << " with: " 
                                                                << state.particles.particles.size() << " particles ( "
                                                                << state.particles.cTot << " cations, " 
                                                                << state.particles.aTot <<" anions)" 
                                                                << std::endl;

        for(unsigned int macro = 0; macro < macroSteps; macro++){
            //printf("Macro\n");
            auto start = std::chrono::steady_clock::now();
            for(unsigned int micro = 0; micro <= microSteps; micro++){
                wIt = std::lower_bound(mWeights.begin(), mWeights.end(), Random::get_random());
                (*moves[wIt - mWeights.begin()])(state.particles.random());
                //printf("accepting\n");
                if(moves[wIt - mWeights.begin()]->accept( state.get_energy_change() )){
                    //printf("saving\n");
                    state.save();
                }
                else{
                    //printf("reverting\n");
                    state.revert();
                }

                    //should also be able to
                    //state.get_energy(subset_of_particles);

                if(macro >= eqSteps){
                    for(auto s : sampler){
                        if(micro % s->interval == 0){
                            s->sample(state);  
                        }
                    }
                }
            }

            /*                                "HALF TIME"                                  */
            //Check energy drift etc
            state.control();
            state.advance();
            //Print progress
            std::cout << "\nIteration (macrostep): " << macro << std::endl;

            printf("Acceptance ratios: \n");
            /*for(auto move : moves){
                printf("%s %.1lf%% %i(%i) ", move->id.c_str(), (double)move->accepted / move->attempted * 100.0, move->attempted, move->accepted);
            }*/

            for(auto move : moves){
                std::cout << move->dump() << std::endl;
            }
            
            printf("Total energy is: %lf, energy drift: %.15lf\n", state.energy, state.error);
            printf("Cations: %i Anions: %i Tot: %i\n", state.particles.cTot, state.particles.aTot, state.particles.tot);
            printf("Box: %lf (%.15lf * %lf * %lf (%lf))\n", state.geo->volume, state.geo->_d[0], state.geo->_d[1], state.geo->_d[2], state.geo->d[2]);
            //printf("Box: %lf * %lf * %lf\n", state.geo->d[0], state.geo->d[1], state.geo->d[2]);
            //printf("Chemical potential: %lf\n\n", constants::cp);
            auto end = std::chrono::steady_clock::now();
            std::cout << (double) std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0 << "s per macrostep\n\n";

            //1. Lista/vektor med olika input som de olika samplingsmetoderna behöver
            //2. sampler kan på något sätt efterfråga input, text genom att sätta en variabel
            //   Sen kan simulator ha en map och leta på den variabeln

            //for(auto s : sampler){
            //    s.sample(??????);
            //    s.sample(s.arguments);
            //}
            for(auto s : sampler){
                s->save();
            }
        }
        /*printf("Saving analysis data...\n");
        for(auto s : sampler){
            s->save(this->name);
        }*/
        for(auto s : sampler){
            s->close();
        }
        IO::to_xyz(this->name, state.particles, state.geo->d);
        IO::to_cpt(this->name, state.particles, state.geo->d);
        IO::to_gro(this->name, state.particles, state.geo->d);
        //this->state.close();
        printf("Energy of last frame: %.15lf\n", this->state.cummulativeEnergy);
        printf("Simulation Done!\n\n");
    }
};



#ifndef PY11
int main(){

    //trans.operator()<decltype(ps[1])>(ps[0]);
    std::string infile = "internal_test_1000K2_cont9_no_rec_gnu.cp";
    std::string outfile = "internal_test_1000K2_cont9_no_rec";

    double cutoff = 27.5;
    Simulator* sim = new Simulator(2.0, 1000.0, outfile);

    sim->state.set_geometry(2, std::vector<double>{172, 172, 55});
    sim->state.set_energy(2, std::vector<double>{cutoff, 7, 7, 7, constants::PI / cutoff});
    //sim->state.particles.create(200, 200, 1.0, -1.0 , 0.5, 2.5, 2.5, 2.5, 0.0, 0.0);
    sim->state.particles.read_cp(infile);

    sim->add_move(0, 0.12, 0.49);
    sim->add_move(0, 25.0, 0.01);
    sim->add_move(1, 0.0, 0.25, -16.0, 0.0);
    sim->add_move(2, 0.0, 0.25, -16.0, 0.0);

    sim->add_sampler(0, 100);
    sim->add_sampler(2, 100);
    //sim->add_sampler(5, 100);
    sim->add_sampler(6, 100);

    sim->state.equilibrate(10.0);

    //After equilibrate
    sim->finalize();
    sim->run(5000, 10000, 0);
    //sim->state.particles.to_xyz("hej.xyz");
    //std::function<void(std::vector<int>)> move_callback = [state](std::vector<int> indices) { state.move_callback(indices); }

    return 0;
}
#endif

#ifdef PY11
PYBIND11_MODULE(mormon, m) {
    
    py::class_<Simulator>(m, "Simulator")
        .def(py::init<double, double, std::string>())
        .def("run", &Simulator::run)
        .def("add_move", &Simulator::add_move, py::arg("i"), py::arg("dp"), py::arg("p"), py::arg("cp") = 0.0, py::arg("d") = 0.0)
        .def("add_sampler", &Simulator::add_sampler)
        .def("set_temperature", &Simulator::set_temperature)
        .def("set_cp", &Simulator::set_cp)
        .def("finalize", &Simulator::finalize)
        .def_readwrite("state", &Simulator::state);


    py::class_<State>(m, "State")
        .def("set_geometry", &State::set_geometry)
        .def("set_energy", &State::set_energy)
        .def("equilibrate", &State::equilibrate)
        .def("load_spline", &State::load_spline)
        .def("reset_energy", &State::reset_energy)
        .def_readwrite("particles", &State::particles)
        .def_readonly("energy", &State::energy)
        .def_readonly("cummulativeEnergy", &State::cummulativeEnergy);


    py::class_<Particles>(m, "Particles")
        .def_readonly("particles", &Particles::particles)
        .def_readonly("pModel", &Particles::pModel)
        .def_readonly("nModel", &Particles::nModel)
        .def("load", &Particles::load)
        .def("create", &Particles::create, py::arg("pNum"), py::arg("nNum"), py::arg("p"), py::arg("n"), py::arg("rfp") = 2.5, py::arg("rfn") = 2.5, py::arg("rp") = 2.5, py::arg("rn") = 2.5, py::arg("bp") = 0.0, py::arg("bn") = 0.0);

    py::class_<Particle>(m, "Particle")
        .def_readonly("com", &Particle::com)
        .def_readonly("pos", &Particle::pos)
        .def_readonly("qDisp", &Particle::qDisp)
        .def_readonly("q", &Particle::q)
        .def_readonly("b", &Particle::b)
        .def_readonly("r", &Particle::r)
        .def_readonly("rf", &Particle::rf);

//    py::class_<Random>(m, "Random")
//        .def("get_random", &Random::get_random);


}
#endif