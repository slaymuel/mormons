#pragma once

#include <vector>
#include "particles.h"
#include <limits>
//#include <math.h>
#include "geometry.h"
#include "energy.h"
#include "potentials.h"

class State{
    private:

    std::shared_ptr<State> _old;

    public:

    ~State(){
        delete geo;
    }

    double energy = 0.0, cummulativeEnergy = 0.0, dE = 0.0, error = 0.0;
    Particles particles;
    std::vector< int > movedParticles;    //Particles that has moved from previous state
    Geometry *geo;
    std::vector< std::shared_ptr<EnergyBase> > energyFunc;
    

    void control(){
        printf("Control\n");
        energy = 0.0;
        for(auto e : energyFunc){
            energy += e->all2all(this->particles);
        }

        error = std::fabs((energy - cummulativeEnergy) / energy);

        if(this->particles.tot != _old->particles.tot){
            printf("_old state has %i particles and current state has %i.\n", _old->particles.tot, this->particles.tot);
            exit(1);
        }

        for(int i = 0; i < this->particles.tot; i++){
            if(this->particles.particles[i]->pos != this->_old->particles.particles[i]->pos){
                printf("current positions is not equal to old positions for particle %i.\n", i);
                std::cout << this->particles.particles[i]->pos << std::endl;
                std::cout << "\n" << this->_old->particles.particles[i]->pos<< std::endl;
                exit(1);
            }
            if(this->particles.particles[i]->index != i){
                printf("index is wrong in current for particle %i, it has index %i.\n", i, this->particles.particles[i]->index );
            }
            if(this->_old->particles.particles[i]->index != i){
                printf("index is wrong in current for particle %i, it has index %i.\n", i, this->_old->particles.particles[i]->index );
            }
        }

        if(this->particles.cTot + this->particles.aTot != this->particles.tot){
            printf("Ctot + aTot != tot\n");
            exit(1);
        }

        if(error > 1e-10 || energy > 1e30){
            printf("\n\nEnergy drift is too large: %.12lf\n\n", error);
            exit(1);
        } 
    }

    void finalize(){
        _old = std::make_shared<State>();

        for(std::shared_ptr<Particle> p : this->particles.particles){
            _old->particles.add(p);
        }

        //Calculate the initial energy of the system
        for(auto e : energyFunc){
            e->initialize(particles);
            this->energy += e->all2all(this->particles);
        }
        this->cummulativeEnergy = this->energy;
    }

    void save(){
        //printf("Save:\n");
        //printf("current moved %lu\n", this->movedParticles.size());
        //printf("old moved %lu\n\n", this->_old->movedParticles.size());
        for(auto i : this->movedParticles){
            if(this->particles.tot > this->_old->particles.tot){
                //printf("\nSave: adding particle %i to old\n\n", i);
                this->_old->particles.add(this->particles.particles[i]);
            }
            else if(this->particles.tot == this->_old->particles.tot){
                *(this->_old->particles.particles[i]) = *(this->particles.particles[i]);
            }
        }

        for(auto i : this->_old->movedParticles){
            if(this->particles.tot < this->_old->particles.tot){
                //printf("\nSave: removing particle %i from old\n\n", i);
                this->_old->particles.remove(i);
            }
        }

        movedParticles.clear();
        this->_old->movedParticles.clear();
        cummulativeEnergy += this->dE;
    }


    void revert(){
        //Set moved partiles in current state equal to previous state
        //also need to set volume and maybe other properties
        for(auto e : energyFunc){
            e->update( this->particles.get_subset(this->movedParticles), this->_old->particles.get_subset(this->_old->movedParticles) );
        }

        for(auto i : this->movedParticles){
            if(this->particles.tot > _old->particles.tot){
                //printf("\nReject: removing particle %i from current\n\n", i);
                this->particles.remove(i);
            }
            else if(this->particles.tot == this->_old->particles.tot){
                //printf("\nAssuming normal move\n");
                *(this->particles.particles[i]) = *(_old->particles.particles[i]);
            }
        }
        //                                                                                      REARRANGE, MOVE CONDITION OUTSIDE OF LOOP
        for(auto i : this->_old->movedParticles){
            if(this->particles.tot < this->_old->particles.tot){
                //printf("\nRevert: adding back particle %i to current\n", i);
                this->particles.add(this->_old->particles.particles[i], i);
            }

        }

        movedParticles.clear();
        this->_old->movedParticles.clear();
    }


    //Get energy different between this and old state
    double get_energy_change(){
        double E1 = 0.0, E2 = 0.0;

        for(auto p : movedParticles){
            if(!this->geo->is_inside(this->particles.particles[p]) || this->overlap(this->particles.particles[p]->index)){
                //If moved outside box or overlap, return inf
                                                                // SHOULD NOT BE HERE, PLZ CHANGE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                for(auto e : energyFunc){
                    e->update( this->_old->particles.get_subset(this->_old->movedParticles), this->particles.get_subset(this->movedParticles) );
                }
                return std::numeric_limits<double>::infinity();
            }
        }
        for(auto e : energyFunc){
            E1 += (*e)( this->_old->movedParticles, this->_old->particles );
            e->update( this->_old->particles.get_subset(this->_old->movedParticles), this->particles.get_subset(this->movedParticles) );
            E2 += (*e)( this->movedParticles, this->particles );
            //std::cout << "old: " << E1 << " new: " << E2 << "\n";
        }
        //printf("Calculated current\n");
        this->dE = E2 - E1;

        //printf("Energy change done\n");
        return this->dE;
    }


    //Called when a move is accepted - set movedParticles
    void move_callback(std::vector< int > ps){   
        // Can do PBC here
        // CHECK FOR OVERLAPS SOMWHERE HERE TO AVOID UNECESSARY COMPUTATIONS (EWALD RECIPROCAL SUM ETC)

        //this->movedParticles.insert(std::end(movedParticles), std::begin(ps), std::end(ps));
        if(this->particles.tot >= this->_old->particles.tot){
            std::for_each(std::begin(ps), std::end(ps), [this](int i){ 
                                                    this->movedParticles.push_back(i); });
        }

        std::copy_if(ps.begin(), ps.end(), std::back_inserter(this->_old->movedParticles), [this](int i){ return i < this->_old->particles.tot; });

        //printf("Callback done\n");
    }


    void load_state(std::vector<double> pos){
        for(auto p : pos){
            std::cout << p << std::endl;
        }
    }

    void add_particle(){
        //this->particles.add(this->geo->random_pos(), p->r, p->q, p->b, p->name);
    }

    void add_images(){
        for(int i = 0; i < this->particles.pTot; i++){
            this->particles.add(this->geo->mirror(this->particles.particles[i]->pos), this->particles.particles[i]->r, 
                                                 -this->particles.particles[i]->q,    this->particles.particles[i]->b, 
                                                  this->particles.particles[i]->name + "I", true);
        }
    }


    void equilibrate(){
        printf("Equilibrating:\n");
        Eigen::Vector3d v;

        for(int i = 0; i < this->particles.pTot; i++){
            this->particles.particles[i]->pos = this->geo->random_pos();
        }
        
        int i = 0, overlaps = 1;
        Eigen::Vector3d oldPos;
        std::shared_ptr<Particle> p;

        //Move particles to prevent overlap
        while(overlaps > 0){
            p = this->particles.random();
            oldPos = p->pos;
            p->translate(10.0);
            if(this->overlap(p->index) || !this->geo->is_inside(p)){
                p->pos = oldPos;
            }


            if(i % 50000 == 0){
                overlaps = this->get_overlaps();
                printf("\tOverlaps: %i, iteration: %i\r", overlaps, i);
                fflush(stdout);
            }
            i++;
            if(i > 1E9){
                i = 0;
            }
        }
        printf("\nEquilibration done\n\n");
    }



    bool overlap(std::size_t i){
        for(auto p : this->particles.particles){
            if(p->index == i) continue;

            if(this->geo->distance(p->pos, this->particles.particles[i]->pos) <= p->r + this->particles.particles[i]->r){
                return true;
            }
        }
        return false;
    }

    int get_overlaps(){
        int count = 0;
        for(auto p : this->particles.particles){
            (this->overlap(p->index)) ? count++ : 0;
        }
        return count;
    }

    void set_geometry(int type){

        switch (type){
            default:
                printf("Creating Cuboid box\n");
                this->geo = new Cuboid<true, true, true>(50.0, 50.0, 50.0);
                break;
            case 1:
                this->geo = new Sphere();
                break;
        }
    }
    


    void set_energy(int type){
        switch (type){
            case 1:
                printf("Adding Ewald potential\n");
                //this->energyFunc.push_back( std::make_shared< PairEnergy<EwaldShort> >() );
                //this->energyFunc.back()->set_geo(this->geo);
                this->energyFunc.push_back( std::make_shared< ExtEnergy<EwaldLong> >(geo->d[0], geo->d[1], geo->d[2]) );
                this->energyFunc.back()->set_geo(this->geo);
                break;

            
            default:
                printf("Adding Coulomb potential\n");
                this->energyFunc.push_back( std::make_shared< PairEnergy<Coulomb> >() );
                this->energyFunc.back()->set_geo(this->geo);
                break;
            
        }
        
    }
};