﻿/*!
 * \file solver_gradient_smoothing.cpp
 * \brief Main subroutines for the gradient smoothing problem.
 * \author T. Dick
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

#include "../include/solver_structure.hpp"
#include "../../Common/include/adt_structure.hpp"
#include <algorithm>


CGradientSmoothingSolver::CGradientSmoothingSolver(CGeometry *geometry, CConfig *config) : CSolver(false,true) {

  unsigned short iDim, jDim;
  unsigned int iElem;

  /*--- general geometric settings ---*/
  nDim         = geometry->GetnDim();
  nPoint       = geometry->GetnPoint();
  nPointDomain = geometry->GetnPointDomain();
  nElement     = geometry->GetnElem();

  /*--- Element container structure ---*/

  /*--- First level: only the FEA_TERM is considered ---*/
  element_container = new CElement** [12];
  element_container[GRAD_TERM] = new CElement* [MAX_FE_KINDS];

  /*--- Initialize all subsequent levels ---*/
  for (unsigned short iKind = 0; iKind < MAX_FE_KINDS; iKind++) {
    element_container[GRAD_TERM][iKind] = NULL;
  }

  if (nDim == 2) {
      element_container[GRAD_TERM][EL_TRIA] = new CTRIA1(nDim, config);
      element_container[GRAD_TERM][EL_QUAD] = new CQUAD4(nDim, config);
  }
  else if (nDim == 3) {
      element_container[GRAD_TERM][EL_TETRA] = new CTETRA1(nDim, config);
      element_container[GRAD_TERM][EL_HEXA]  = new CHEXA8(nDim, config);
      element_container[GRAD_TERM][EL_PYRAM] = new CPYRAM5(nDim, config);
      element_container[GRAD_TERM][EL_PRISM] = new CPRISM6(nDim, config);
  }

  /*--- Allocate element properties - only the index, to allow further integration with CFEASolver on a later stage ---*/
  element_properties = new CProperty*[nElement];
  for (iElem = 0; iElem < nElement; iElem++){
    element_properties[iElem] = new CProperty(iElem);
  }

  /*--- auxiliary submatrices ---*/
  #ifndef CODI_FORWARD_TYPE
    passivedouble **matrixId;
  #else
    su2double **matrixId;
  #endif

  /*--- Matrices to impose boundary conditions ---*/

  mZeros_Aux = new su2double *[nDim];
  mId_Aux    = new su2double *[nDim];
  for(iDim = 0; iDim < nDim; iDim++){
    mZeros_Aux[iDim] = new su2double[nDim];
    mId_Aux[iDim]    = new su2double[nDim];
  }

  for(iDim = 0; iDim < nDim; iDim++){
    for (jDim = 0; jDim < nDim; jDim++){
      mZeros_Aux[iDim][jDim] = 0.0;
      mId_Aux[iDim][jDim]    = 0.0;
    }
    mId_Aux[iDim][iDim] = 1.0;
  }

  /*--- Term ij of the Jacobian ---*/
  Jacobian_ij = new su2double*[nDim];
  for (iDim = 0; iDim < nDim; iDim++) {
    Jacobian_ij[iDim] = new su2double [nDim];
    for (jDim = 0; jDim < nDim; jDim++) {
      Jacobian_ij[iDim][jDim] = 0.0;
    }
  }

  /*--- linear system ---*/
  LinSysSol.Initialize(nPoint, nPointDomain, nDim, 0.0);
  LinSysRes.Initialize(nPoint, nPointDomain, nDim, 0.0);
  Jacobian.Initialize(nPoint, nPointDomain, nDim, nDim, false, geometry, config);

}


CGradientSmoothingSolver::~CGradientSmoothingSolver(void) {

  unsigned short iDim;

  delete [] Residual;
  delete [] matrixId;

  if (element_container != NULL) {
    for (unsigned short iVar = 0; iVar < MAX_TERMS; iVar++) {
      for (unsigned short jVar = 0; jVar < MAX_FE_KINDS; jVar++) {
        if (element_container[iVar][jVar] != NULL) delete element_container[iVar][jVar];
      }
      delete [] element_container[iVar];
    }
    delete [] element_container;
  }

  for (iDim = 0; iDim < nDim; iDim++) {
    delete [] mZeros_Aux[iDim];
    delete [] mId_Aux[iDim];
    delete [] Jacobian_ij[iDim];
  }
  delete [] mZeros_Aux;
  delete [] mId_Aux;
  delete [] Jacobian_ij;

}


void CGradientSmoothingSolver::ApplyGradientSmoothing(CGeometry *geometry, CSolver *solver, CNumerics **numerics, CConfig *config) {

  /*--- Initialize vector and sparse matrix ---*/
  LinSysSol.SetValZero();
  LinSysRes.SetValZero();
  Jacobian.SetValZero();

  Compute_StiffMatrix(geometry, numerics, config);

  Compute_Residual(geometry, solver, config);

  Impose_BC(geometry, numerics, config);

  Solve_Linear_System(geometry, config);

  Set_Sensitivities(geometry, solver, config);

}


void CGradientSmoothingSolver::Compute_StiffMatrix(CGeometry *geometry, CNumerics **numerics, CConfig *config){

  unsigned long iElem, iVar, jVar;
  unsigned short iNode, iDim, jDim, nNodes = 0;
  unsigned long indexNode[8]={0,0,0,0,0,0,0,0};
  su2double val_Coord;
  int EL_KIND = 0;

  su2double *Ta = NULL;
  su2double **DHiDHj = NULL;
  su2double *DHiHj = NULL;
  su2double HiHj = 0.0;

  unsigned short NelNodes, jNode;

  /*--- Loops over all the elements ---*/

  for (iElem = 0; iElem < geometry->GetnElem(); iElem++) {

    if (geometry->elem[iElem]->GetVTK_Type() == TRIANGLE)      {nNodes = 3; EL_KIND = EL_TRIA;}
    if (geometry->elem[iElem]->GetVTK_Type() == QUADRILATERAL) {nNodes = 4; EL_KIND = EL_QUAD;}
    if (geometry->elem[iElem]->GetVTK_Type() == TETRAHEDRON)   {nNodes = 4; EL_KIND = EL_TETRA;}
    if (geometry->elem[iElem]->GetVTK_Type() == PYRAMID)       {nNodes = 5; EL_KIND = EL_PYRAM;}
    if (geometry->elem[iElem]->GetVTK_Type() == PRISM)         {nNodes = 6; EL_KIND = EL_PRISM;}
    if (geometry->elem[iElem]->GetVTK_Type() == HEXAHEDRON)    {nNodes = 8; EL_KIND = EL_HEXA;}

    for (iNode = 0; iNode < nNodes; iNode++) {

      indexNode[iNode] = geometry->elem[iElem]->GetNode(iNode);

      for (iDim = 0; iDim < nDim; iDim++) {
        val_Coord = Get_ValCoord(geometry, indexNode[iNode], iDim);
        element_container[GRAD_TERM][EL_KIND]->SetRef_Coord(val_Coord, iNode, iDim);
      }

    }

    /*--- compute the contributions of the single elements inside the numerics container ---*/

    numerics[GRAD_TERM]->Compute_Tangent_Matrix(element_container[GRAD_TERM][EL_KIND], config);

    NelNodes = element_container[GRAD_TERM][EL_KIND]->GetnNodes();

    /*--- for all nodes add the contribution to the system Jacobian ---*/

    for (iNode = 0; iNode < NelNodes; iNode++) {

      for (jNode = 0; jNode < NelNodes; jNode++) {

        DHiDHj = element_container[GRAD_TERM][EL_KIND]->Get_DHiDHj(iNode, jNode);
        DHiHj = element_container[GRAD_TERM][EL_KIND]->Get_DHiHj(iNode, jNode);
        HiHj = element_container[Gradient_Map][EL_KIND]->GetDHiHj(iNode, jNode);

        for (iVar = 0; iVar < nVar; iVar++) {
          for (jVar = 0; jVar < nVar; jVar++) {
            Jacobian_ij[iVar][jVar] = DHiDHj[iVar][jVar];
          }
          Jacobian_ij[iVar][iVar] -= HiHj;
        }
        Jacobian.AddBlock(indexNode[iNode], indexNode[jNode], Jacobian_ij);
      }

    }

  }


/*

  unsigned long iPoint;

  matrixId    = new su2double *[nDim];
  for(iDim = 0; iDim < nDim; iDim++){
    matrixId[iDim]    = new su2double[nDim];
  }
  for(iDim = 0; iDim < nDim; iDim++){
    for (jDim = 0; jDim < nDim; jDim++){
      matrixId[iDim][jDim]    = 0.0;
    }
    matrixId[iDim][iDim] = 2.0;
  }

  for (iPoint = 0; iPoint < nPoint; iPoint++) {
    Jacobian.SetBlock(iPoint,iPoint,matrixId);
  }

*/
}


void CGradientSmoothingSolver::Compute_Residual(CGeometry *geometry, CSolver *solver, CConfig *config){

/*
  unsigned long iPoint;
  unsigned short iDim;
  Residual = new su2double [nDim];

  for (iPoint = 0; iPoint < nPoint; iPoint++) {
    for (iDim = 0; iDim < nDim; iDim++) {
      Residual[iDim] = solver->node[iPoint]->GetSensitivity(iDim);
      Residual[iDim] = Residual[iDim] * element_container[GRAD_TERM][EL_KIND]->GetNi(iPoint,iDim);
    }
    LinSysRes.SetBlock(iPoint, Residual);
  }
*/


  unsigned short nNode = element->GetnNodes();
  unsigned short nGauss = element->GetnGaussPoints();


  for (iElem = 0; iElem < geometry->GetnElem(); iElem++) {

    if (geometry->elem[iElem]->GetVTK_Type() == TRIANGLE)      {nNodes = 3; EL_KIND = EL_TRIA;}
    if (geometry->elem[iElem]->GetVTK_Type() == QUADRILATERAL) {nNodes = 4; EL_KIND = EL_QUAD;}
    if (geometry->elem[iElem]->GetVTK_Type() == TETRAHEDRON)   {nNodes = 4; EL_KIND = EL_TETRA;}
    if (geometry->elem[iElem]->GetVTK_Type() == PYRAMID)       {nNodes = 5; EL_KIND = EL_PYRAM;}
    if (geometry->elem[iElem]->GetVTK_Type() == PRISM)         {nNodes = 6; EL_KIND = EL_PRISM;}
    if (geometry->elem[iElem]->GetVTK_Type() == HEXAHEDRON)    {nNodes = 8; EL_KIND = EL_HEXA;}

    for (unsigned short iGauss = 0; iGauss < nGauss; iGauss++) {

      for (iNode = 0; iNode < nNodes; iNode++) {
        indexNode[iNode] = geometry->elem[iElem]->GetNode(iNode);
      }

      Weight = element_container[GRAD_TERM][EL_KIND]->GetWeight(iGauss);
      Jac_X = element->GetJ_X(iGauss);      /*--- The mass matrix is computed in the reference configuration ---*/

      for (unsigned short iNode = 0; iNode < nNode; iNode++) {
        Residual[iDim] = Weight * Jac_X *
            element->GetNi(iNode,iGauss); * solver->node[indexNode[iNode]]->GetSensitivity(iDim);
      }
      // is this correct recalculation of the node?
      LinSysRes.SetBlock(indexNode[iNode], Residual);
    }
  }
}


void CGradientSmoothingSolver::Impose_BC(CGeometry *geometry, CNumerics **numerics, CConfig *config) {

  unsigned short iMarker;

  /*--- Get the boundary markers and iterate over them ---*/

  for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {

    if (config->GetMarker_All_SobolevBC(iMarker) == NO) {
      BC_Dirichlet(geometry, NULL, numerics, config, iMarker);
    }

    if (config->GetMarker_All_SobolevBC(iMarker) == YES) {
      BC_Neumann(geometry, NULL, numerics, config, iMarker);
    }

  }

}


void CGradientSmoothingSolver::BC_Dirichlet(CGeometry *geometry, CSolver **solver_container, CNumerics **numerics, CConfig *config, unsigned short val_marker) {


  unsigned long iPoint, iVertex;
  unsigned long iVar, jVar;

  for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {

    /*--- Get node index ---*/

    iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

    if (geometry->node[iPoint]->GetDomain()) {

      if (nDim == 2) {
        Solution[0] = 0.0;  Solution[1] = 0.0;
        Residual[0] = 0.0;  Residual[1] = 0.0;
      }
      else {
        Solution[0] = 0.0;  Solution[1] = 0.0;  Solution[2] = 0.0;
        Residual[0] = 0.0;  Residual[1] = 0.0;  Residual[2] = 0.0;
      }

      LinSysRes.SetBlock(iPoint, Residual);
      LinSysSol.SetBlock(iPoint, Solution);

      /*--- STRONG ENFORCEMENT OF THE DIRICHLET BOUNDARY CONDITION ---*/

      /*--- Delete the columns for a particular node ---*/

      for (iVar = 0; iVar < nPoint; iVar++) {
        if (iVar==iPoint) {
          Jacobian.SetBlock(iVar,iPoint,mId_Aux);
        }
        else {
          Jacobian.SetBlock(iVar,iPoint,mZeros_Aux);
        }
      }

      /*--- Delete the rows for a particular node ---*/
      for (jVar = 0; jVar < nPoint; jVar++) {
        if (iPoint!=jVar) {
          Jacobian.SetBlock(iPoint,jVar,mZeros_Aux);
        }
      }

    } else {

      /*--- Delete the column (iPoint is halo so Send/Recv does the rest) ---*/

      for (iVar = 0; iVar < nPoint; iVar++) Jacobian.SetBlock(iVar,iPoint,mZeros_Aux);

    }

  }

}


void CGradientSmoothingSolver::BC_Neumann(CGeometry *geometry, CSolver **solver_container, CNumerics **numerics, CConfig *config, unsigned short val_marker) {

}


void CGradientSmoothingSolver::Solve_Linear_System(CGeometry *geometry, CConfig *config){

  unsigned long IterLinSol = 0;

  IterLinSol = System.Solve(Jacobian, LinSysRes, LinSysSol, geometry, config);

  SetIterLinSolver(IterLinSol);

  // valResidual = System.GetResidual();

}


void CGradientSmoothingSolver::Set_Sensitivities(CGeometry *geometry, CSolver *solver, CConfig *config){

  unsigned long iPoint, total_index;
  unsigned short iDim;

  for (iPoint = 0; iPoint < nPoint; iPoint++) {
    for (iDim = 0; iDim < nDim; iDim++) {
      total_index = iPoint*nDim + iDim;
      solver->node[iPoint]->SetSensitivity(iDim,LinSysSol[total_index]);
    }
  }

}

