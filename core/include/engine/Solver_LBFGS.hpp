#pragma once
#ifndef SOLVER_LBFGS_HPP
#define SOLVER_LBFGS_HPP

#include <utility/Constants.hpp>
// #include <utility/Exception.hpp>
#include <algorithm>

using namespace Utility;

template <> inline
void Method_Solver<Solver::LBFGS>::Initialize ()
{
    this->jmax = 500;    // max iterations
    this->n    = 50;     // restart every n iterations XXX: what's the appropriate val?
    this->n_lbfgs_memory = 10; // how many updates the solver tracks to estimate the hessian
    this->n_updates = intfield( this->noi, 0 );

    this->a_updates    = std::vector<std::vector<vectorfield>>( this->noi, std::vector<vectorfield>( this->n_lbfgs_memory, vectorfield(this->nos, { 0,0,0 } ) ));
    this->grad_updates = std::vector<std::vector<vectorfield>>( this->noi, std::vector<vectorfield>( this->n_lbfgs_memory, vectorfield(this->nos, { 0,0,0 } ) ));
    this->rho_temp     = std::vector<scalarfield>( this->noi, scalarfield( this->n_lbfgs_memory, 0 ) );
    this->alpha_temp   = std::vector<scalarfield>( this->noi, scalarfield( this->n_lbfgs_memory, 0 ) );

    // Polak-Ribiere criterion
    this->beta  = scalarfield( this->noi, 0 );

    this->forces                   = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->forces_virtual           = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->forces_displaced         = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->a_coords                 = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->a_coords_displaced       = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->a_directions             = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->a_direction_norm         = scalarfield     ( this->noi, 0 );

    this->a_residuals              = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->a_residuals_last         = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->a_residuals_displaced    = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );

    this->configurations_displaced = std::vector<std::shared_ptr<vectorfield>>( this->noi );
    this->reference_configurations = std::vector<std::shared_ptr<vectorfield>>( this->noi );
    this->E0                       = scalarfield(this->noi, 0);
    this->g0                       = scalarfield(this->noi, 0);
    this->finish                   = std::vector<bool>( this->noi, false );
    this->step_size                = scalarfield(this->noi, 0);
    for (int i=0; i<this->noi; i++)
    {
        configurations_displaced[i] = std::shared_ptr<vectorfield>( new vectorfield( this->nos, {0, 0, 0} ) );
        reference_configurations[i] = std::shared_ptr<vectorfield>( new vectorfield( *this->configurations[i]) );
    }
};

/*
    Implemented according to Aleksei Ivanov's paper: https://arxiv.org/abs/1904.02669
    TODO: reference painless conjugate gradients
    See also Jorge Nocedal and Stephen J. Wright 'Numerical Optimization' Second Edition, 2006 (p. 121)

    Template instantiation of the Simulation class for use with the NCG Solver
    The method of nonlinear conjugate gradients is a proven and effective solver.
*/

template <> inline
void Method_Solver<Solver::LBFGS>::Iteration()
{
    // Current force
    this->Calculate_Force( this->configurations, this->forces );

    #pragma omp parallel for
    for( int img=0; img<this->noi; img++ )
    {
        auto& image                 = *this->configurations[img];
        auto& image_displaced       = *this->configurations_displaced[img];
        auto& beta                  = this->beta[img];
        auto& a_coords              = this->a_coords[img];
        auto& a_coords_displaced    = this->a_coords_displaced[img];
        auto& a_directions          = this->a_directions[img];
        auto& a_residuals           = this->a_residuals[img];
        auto& a_residuals_last      = this->a_residuals_last[img];
        auto& a_residuals_displaced = this->a_residuals_displaced[img];

        // Update virtual force
        for(int i=0; i<this->nos; ++i)
            this->forces_virtual[img][i] = image[i].cross(this->forces[img][i]);

        // Calculate residuals for current parameters and save the old residuals
        Solver_Kernels::ncg_OSO_residual(a_residuals, a_residuals_last, image, a_coords, this->forces[img]);

        // Keep track of residual updates
        if(this->iteration > 0)
        {
            int idx = (this->iteration-1) % n_lbfgs_memory;
            #pragma omp parallel for
            for(int i=0; i<this->nos; i++)
            {
                this->grad_updates[img][idx][i] = a_residuals[i] - a_residuals_last[i];
            }

            this->rho_temp[img][idx] = 1/Vectormath::dot(this->grad_updates[img][idx], this->a_updates[img][idx]);
        }

        // Reset direction to steepest descent if line search failed
        if(!this->finish[img])
        {
            fmt::print("resetting\n");
            this->n_updates[img] = 0;
        }

        // Calculate new search direction
        Solver_Kernels::lbfgs_get_descent_direction(this->iteration, this->n_updates[img], a_directions, a_residuals, this->a_updates[img], this->grad_updates[img], this->rho_temp[img], this->alpha_temp[img]);
        this->a_direction_norm[img] = Manifoldmath::norm( a_directions );

        if(this->n_updates[img] < this->n_lbfgs_memory)
           this->n_updates[img]++;


        // Debug
        // for(int i=0; i<n_lbfgs_memory; i++)
        //     fmt::print("rho_temp[{}] = {}\n", i, this->rho_temp[img][i]);

        // for(int i=0; i<this->nos; i++)
        // {
        //     fmt::print("a_direction[{}] = [{}, {}, {}]\n", i, this->a_directions[img][i][0], this->a_directions[img][i][1], this->a_directions[img][i][2]);

        //     // if(std::isnan(this->a_directions[img][i][0]))
        //     //     throw 0;
        // }

        // Before the line search, set step_size and precalculate E0 and g0
        step_size[img] = 1.0;
        E0[img]        = this->systems[img]->hamiltonian->Energy(image);
        g0[img]        = 0;

        #pragma omp parallel for reduction(+:g0)
        for( int i=0; i<this->nos; ++i )
        {
            g0[img] -= a_residuals[i].dot(a_directions[i]) / a_direction_norm[img];
            this->finish[img] = false;
        }
    }

    int n_search_steps     = 0;
    int n_search_steps_max = 20;
    bool run = true;;
    while(run)
    {
        Solver_Kernels::ncg_OSO_displace(this->configurations_displaced, this->reference_configurations, this->a_coords,
                                        this->a_coords_displaced, this->a_directions, this->finish, this->step_size);

        // Calculate forces for displaced spin directions
        this->Calculate_Force( configurations_displaced, forces_displaced );

        Solver_Kernels::ncg_OSO_line_search( this->configurations_displaced, this->a_coords_displaced, this->a_directions,
                             this->forces_displaced, this->a_residuals_displaced, this->systems, this->finish, E0, g0, this->a_direction_norm, step_size );

        n_search_steps++;
        fmt::print("line search step {}\n", n_search_steps);

        run = (n_search_steps >= n_search_steps_max) ? false : true;
        for(int img=0; img<this->noi; img++)
            run = (run && !finish[img]);
    }

    for (int img=0; img<this->noi; img++)
    {
        auto& image              = *this->configurations[img];
        auto& image_displaced    = *this->configurations_displaced[img];
        auto& a_coords           = this->a_coords[img];
        auto& a_coords_displaced = this->a_coords_displaced[img];

        // Update current image
        for(int i=0; i<image.size(); i++)
        {
            int idx = this->iteration % this->n_lbfgs_memory;
            if(this->finish[img]) // only if line search was successfull
            {
                this->a_updates[img][idx][i] = a_coords_displaced[i] - a_coords[i]; // keep track of a_updates
                a_coords[i] = a_coords_displaced[i];
                image[i]    = image_displaced[i];
            } else {
                this->a_updates[img][idx][i] = {0,0,0}; // keep track of a_updates
            }
        }

        if( this->iteration % this->n == 0)
        {
            Solver_Kernels::ncg_OSO_update_reference_spins(*this->reference_configurations[img], a_coords, image);
        }

    }
}

template <> inline
std::string Method_Solver<Solver::LBFGS>::SolverName()
{
    return "LBFGS";
}

template <> inline
std::string Method_Solver<Solver::LBFGS>::SolverFullName()
{
    return "Limited memory Broyden-Fletcher-Goldfarb-Shanno";
}

#endif