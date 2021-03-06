/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "spherical_harmonics.h"
#include <cmath>
#include <cfloat>
extern "C" {
// Legendre polynomials
#include <gsl/gsl_sf_legendre.h>
}

std::complex<double> spherical_harmonics(int l, int m, double cth, double phi) {
  /* Calculate value of spherical harmonic Y_{lm} = N_{lm} P_{lm} (\cos \theta) e^{i m \phi} */
  if(m<0) { // GSL doesn't handle m<0. Use the identity {Y_l}^{-m} = (-1)^m \overline{ {Y_l}^{m} }
    return conj(pow(-1.0,m)*spherical_harmonics(l,-m,cth,phi));
  }

  std::complex<double> ylm;

  // First, calculate the exponential
  ylm=pow(M_E,std::complex<double>(0.0,m*phi)); // e^(im phi)
  // and then plug in the normalized associated Legendre polynomial,
  // which already seems to take into account the Condon-Shortley
  // phase factor.
  ylm*=gsl_sf_legendre_sphPlm(l,m,cth);

  return ylm;
}
