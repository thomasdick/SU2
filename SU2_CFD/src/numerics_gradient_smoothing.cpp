/*!
 * \file numerics_direct_elasticity.cpp
 * \brief This file contains the routines for setting the tangent matrix and residual of a sobolev gradient problem
 * \author T.Dick
 * \version 6.2.0 "Falcon"
 *
 * The current SU2 release has been coordinated by the
 * SU2 International Developers Society <www.su2devsociety.org>
 * with selected contributions from the open-source community.
 *
 * The main research teams contributing to the current release are:
 *  - Prof. Juan J. Alonso's group at Stanford University.
 *  - Prof. Piero Colonna's group at Delft University of Technology.
 *  - Prof. Nicolas R. Gauger's group at Kaiserslautern University of Technology.
 *  - Prof. Alberto Guardone's group at Polytechnic University of Milan.
 *  - Prof. Rafael Palacios' group at Imperial College London.
 *  - Prof. Vincent Terrapon's group at the University of Liege.
 *  - Prof. Edwin van der Weide's group at the University of Twente.
 *  - Lab. of New Concepts in Aeronautics at Tech. Institute of Aeronautics.
 *
 * Copyright 2012-2019, Francisco D. Palacios, Thomas D. Economon,
 *                      Tim Albring, and the SU2 contributors.
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../include/numerics_structure.hpp"
#include <limits>

CGradSmoothing::CGradSmoothing(void) : CNumerics () {

  GradNi_Ref_Mat = NULL;
  val_DHiDHj = NULL;
  Ni_Vec = NULL;

}

CGradSmoothing::CGradSmoothing(unsigned short val_nDim, CConfig *config)
  : CNumerics(val_nDim, val_nDim, config) {

  unsigned short iNode, iDim;

  /*--- 8 is the max number of nodes in 3D ---*/
  GradNi_Ref_Mat = new su2double* [8];
  val_DHiDHj = new su2double* [nDim];
  for (iDim=0; iDim<nDim; iDim++) {
    val_DHiDHj[iDim] = new su2double[nDim];
  }
  for (iNode = 0; iNode < 8; iNode++) {
    GradNi_Ref_Mat[iNode]   = new su2double[nDim];
  }
  Ni_Vec  = new su2double [8];

}

CGradSmoothing::~CGradSmoothing(void) {

  unsigned short iDim, iNode;

  if (val_DHiDHj != NULL) {
    for (iDim = 0; iDim < nDim; iDim++) {
      if (val_DHiDHj[iDim] != NULL) delete [] val_DHiDHj[iDim];
    }
    delete [] val_DHiDHj;
  }

  if (GradNi_Ref_Mat != NULL) {
    for (iNode = 0; iNode < 8; iNode++) {
      if (GradNi_Ref_Mat[iNode] != NULL) delete [] GradNi_Ref_Mat[iNode];
    }
    delete [] GradNi_Ref_Mat;
  }

  if (Ni_Vec != NULL) delete [] Ni_Vec;

}

void CGradSmoothing::Compute_Tangent_Matrix(CElement *element, CConfig *config) {

  unsigned short iDim, jDim;
  unsigned short iGauss, nGauss;
  unsigned short iShape, jShape, nNode;

  unsigned short nDimGlobal = nDim;   /*--- need a different number of dimensions if we are on a curved surface --*/
  if (config->GetSmoothOnSurface()) nDimGlobal=nDim+1;

  su2double Weight, Jac_X, val_HiHj, GradNiXGradNj = 0;

  su2double epsilon = config->GetSmoothingParam();
  su2double zeta = config->GetSmoothingParamSecond();

  element->clearElement(true);       /*--- Restarts the element: avoids adding over previous results in other elements --*/
  nNode = element->GetnNodes();
  nGauss = element->GetnGaussPoints();
  if (config->GetSmoothOnSurface()) {
    element->ComputeGrad_Linear(Coord);
  } else {
    element->ComputeGrad_Linear();
  }

  /*--- contribution from the gradients of the shape functions ---*/

  for (iGauss = 0; iGauss < nGauss; iGauss++) {

    Weight = element->GetWeight(iGauss);
    Jac_X = element->GetJ_X(iGauss);

    for (iShape = 0; iShape < nNode; iShape++) {
      for (iDim = 0; iDim < nDimGlobal; iDim++) {
        GradNi_Ref_Mat[iShape][iDim] = element->GetGradNi_X(iShape,iGauss,iDim);
      }
    }

    /*
    for (iShape = 0; iShape < nNode; iShape++) {
      std::cout << "Grad for " << iShape << " : ("
                << GradNi_Ref_Mat[iShape][0] << ", "
                << GradNi_Ref_Mat[iShape][1] << ", "
                << GradNi_Ref_Mat[iShape][2] << ", "
                << std::endl;
    }
    */

    for (iShape = 0; iShape < nNode; iShape++) {

      /*--- Assumming symmetry ---*/
      for (jShape = iShape; jShape < nNode; jShape++) {

        for (iDim = 0; iDim < nDimGlobal; iDim++) {
          GradNiXGradNj += GradNi_Ref_Mat[iShape][iDim]* GradNi_Ref_Mat[jShape][iDim];
        }

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0; jDim < nDim; jDim++) {
            if (iDim == jDim) {
              val_DHiDHj[iDim][jDim] = Weight * Jac_X * epsilon * epsilon * GradNiXGradNj;
            } else {
              val_DHiDHj[iDim][jDim] = 0;
            }
          }
        }

        GradNiXGradNj=0;

        element->Add_DHiDHj(val_DHiDHj,iShape, jShape);
        /*--- Symmetric terms --*/
        if (iShape != jShape) {
          element->Add_DHiDHj_T(val_DHiDHj, jShape, iShape);
        }

        //std::cout << iShape << ", " << jShape << ", "<< val_DHiDHj[0][0] <<std::endl;

      }

    }

  }

  /*--- contribution from the shape functions themselfes --*/

  for (iGauss = 0; iGauss < nGauss; iGauss++) {

    Weight = element->GetWeight(iGauss);
    Jac_X = element->GetJ_X(iGauss);

    for (iShape = 0; iShape < nNode; iShape++) {
      Ni_Vec[iShape] = element->GetNi(iShape,iGauss);
    }

    for (iShape = 0; iShape < nNode; iShape++) {
      for (jShape = 0; jShape < nNode; jShape++) {
        val_HiHj = Weight * Jac_X * zeta * Ni_Vec[iShape] * Ni_Vec[jShape];
        element->Add_HiHj(val_HiHj, iShape, jShape);
      }
    }

  }

}

void CGradSmoothing::SetCoord(std::vector<std::vector<su2double>>& val_coord) {
  Coord = val_coord;
}
