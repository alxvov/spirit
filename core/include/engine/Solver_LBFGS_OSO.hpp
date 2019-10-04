#pragma once
#ifndef SOLVER_LBFGS_OSO_HPP
#define SOLVER_LBFGS_OSO_HPP

#include <utility/Constants.hpp>
// #include <utility/Exception.hpp>
#include <algorithm>

using namespace Utility;

template <> inline
void Method_Solver<Solver::LBFGS_OSO>::Initialize ()
{

    this->n_lbfgs_memory = 3; // how many previous iterations are stored in the memory

    this->delta_a = std::vector<std::vector<vectorfield>>(
            this->noi, std::vector<vectorfield>( this->n_lbfgs_memory, vectorfield(this->nos, { 0,0,0 } ) ));
    this->delta_grad = std::vector<std::vector<vectorfield>>(
            this->noi, std::vector<vectorfield>( this->n_lbfgs_memory, vectorfield(this->nos, { 0,0,0 } ) ));
    this->rho = std::vector<scalarfield>( this->noi, scalarfield( this->n_lbfgs_memory, 0 ) );
    this->alpha = std::vector<scalarfield>( this->noi, scalarfield( this->n_lbfgs_memory, 0 ) );

    this->forces = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->forces_virtual = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );

    this->searchdir = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->grad = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->grad_pr = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );
    this->q_vec = std::vector<vectorfield>( this->noi, vectorfield( this->nos, { 0,0,0 } ) );

    this->local_iter = 0;
    this->maxmove = Constants::Pi / 200.0;
};

/*
    Implemented according to Aleksei Ivanov's paper: https://arxiv.org/abs/1904.02669
    TODO: reference painless conjugate gradients
    See also Jorge Nocedal and Stephen J. Wright 'Numerical Optimization' Second Edition, 2006 (p. 121).
*/

template <> inline
void Method_Solver<Solver::LBFGS_OSO>::Iteration()
{
    // update forces which are -dE/ds
    this->Calculate_Force( this->configurations, this->forces );
    // calculate gradients for OSO
    #pragma omp parallel for
    for( int img=0; img < this->noi; img++ )
    {
        auto& image = *this->configurations[img];
        auto& grad_ref = this->grad[img];
        for (int i = 0; i < this->nos; ++i){
            this->forces_virtual[img][i] = image[i].cross(this->forces[img][i]);
        }
        Solver_Kernels::oso_calc_gradients(grad_ref, image, this->forces[img]);
    }

    // calculate search direction
    Solver_Kernels::lbfgs_get_searchdir(this->local_iter,
            this->rho, this->alpha, this->q_vec,
            this->searchdir, this->delta_a,
            this->delta_grad, this->grad, this->grad_pr,
            this->n_lbfgs_memory, maxmove);
    // rotate spins
    Solver_Kernels::oso_rotate( this->configurations, this->searchdir);

}


template <> inline
std::string Method_Solver<Solver::LBFGS_OSO>::SolverName()
{
    return "LBFGS_OSO";
}

template <> inline
std::string Method_Solver<Solver::LBFGS_OSO>::SolverFullName()
{
    return "Limited memory Broyden-Fletcher-Goldfarb-Shanno with exponential transform";
}

#endif