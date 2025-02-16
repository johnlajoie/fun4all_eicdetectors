#include "PHG4ForwardEcalDetector.h"

#include "PHG4ForwardEcalDisplayAction.h"

#include <phparameter/PHParameters.h>

#include <g4gdml/PHG4GDMLConfig.hh>
#include <g4gdml/PHG4GDMLUtility.hh>

#include <g4main/PHG4Detector.h>       // for PHG4Detector
#include <g4main/PHG4DisplayAction.h>  // for PHG4DisplayAction
#include <g4main/PHG4Subsystem.h>

#include <phool/recoConsts.h>

#include <Geant4/G4Box.hh>
#include <Geant4/G4Cons.hh>
#include <Geant4/G4SubtractionSolid.hh>
#include <Geant4/G4LogicalVolume.hh>
#include <Geant4/G4Material.hh>
#include <Geant4/G4PVPlacement.hh>
#include <Geant4/G4PVReplica.hh>
#include <Geant4/G4PhysicalConstants.hh>
#include <Geant4/G4RotationMatrix.hh>  // for G4RotationMatrix
#include <Geant4/G4String.hh>          // for G4String
#include <Geant4/G4SystemOfUnits.hh>
#include <Geant4/G4ThreeVector.hh>  // for G4ThreeVector
#include <Geant4/G4Transform3D.hh>  // for G4Transform3D
#include <Geant4/G4Tubs.hh>
#include <Geant4/G4Types.hh>            // for G4double, G4int
#include <Geant4/G4VPhysicalVolume.hh>  // for G4VPhysicalVolume

#include <TSystem.h>

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>  // for pair, make_pair

class G4VSolid;
class PHCompositeNode;

//_______________________________________________________________________
PHG4ForwardEcalDetector::PHG4ForwardEcalDetector(PHG4Subsystem* subsys, PHCompositeNode* Node, PHParameters* parameters, const std::string& dnam)
  : PHG4Detector(subsys, Node, dnam)
  , m_DisplayAction(dynamic_cast<PHG4ForwardEcalDisplayAction*>(subsys->GetDisplayAction()))
  , m_Params(parameters)
  , m_GdmlConfig(PHG4GDMLUtility::GetOrMakeConfigNode(Node))
  , m_ActiveFlag(m_Params->get_int_param("active"))
  , m_AbsorberActiveFlag(m_Params->get_int_param("absorberactive"))
{
  for (int i = 0; i < 3; i++)
  {
    m_TowerDx[i] = 30 * mm;
    m_TowerDy[i] = 30 * mm;
    m_TowerDz[i] = 170.0 * mm;
  }
  for (int i = 3; i < 7; i++)
  {
    m_TowerDx[i] = NAN;
    m_TowerDy[i] = NAN;
    m_TowerDz[i] = NAN;
  }
  m_RMin[0] = 110 * mm;
  m_RMax[0] = 2250 * mm;
  m_RMin[1] = 120 * mm;
  m_RMax[1] = 2460 * mm;
  m_Params->set_double_param("xoffset", 0.);
  m_Params->set_double_param("yoffset", 0.);

  assert(m_GdmlConfig);
}

//_______________________________________________________________________
int PHG4ForwardEcalDetector::IsInForwardEcal(G4VPhysicalVolume* volume) const
{
  G4LogicalVolume* mylogvol = volume->GetLogicalVolume();
  if (m_ActiveFlag)
  {
    if (m_ScintiLogicalVolSet.find(mylogvol) != m_ScintiLogicalVolSet.end())
    {
      return 1;
    }
  }
  if (m_AbsorberActiveFlag)
  {
    if (m_AbsorberLogicalVolSet.find(mylogvol) != m_AbsorberLogicalVolSet.end())
    {
      return -1;
    }
  }
  return 0;
}

//_______________________________________________________________________
void PHG4ForwardEcalDetector::ConstructMe(G4LogicalVolume* logicWorld)
{
  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Begin Construction" << std::endl;
  }

  /* Read parameters for detector construction and mappign from file */
  ParseParametersFromTable();

  /* Create the cone envelope = 'world volume' for the crystal calorimeter */
  recoConsts* rc = recoConsts::instance();
  G4Material* WorldMaterial = G4Material::GetMaterial(rc->get_StringFlag("WorldMaterial"));

  
  G4VSolid *beampipe_cutout = new G4Cons("FEMC_beampipe_cutout",
                                         0, m_RMin[0],
                                         0, m_RMin[1],
                                         m_dZ / 2.0,
                                         0, 2 * M_PI);
  G4VSolid *ecal_envelope_solid = new G4Cons("FEMC_envelope_solid_cutout",
                                            0, m_RMax[0],
                                            0, m_RMax[1],
                                            m_dZ / 2.0,
                                            0, 2 * M_PI);
  ecal_envelope_solid = new G4SubtractionSolid(G4String("hFEMC_envelope_solid"), ecal_envelope_solid, beampipe_cutout, 0, G4ThreeVector(m_Params->get_double_param("xoffset") * cm, m_Params->get_double_param("yoffset") * cm, 0.));


  G4LogicalVolume* ecal_envelope_log = new G4LogicalVolume(ecal_envelope_solid, WorldMaterial, "hFEMC_envelope", 0, 0, 0);

  /* Define visualization attributes for envelope cone */
  GetDisplayAction()->AddVolume(ecal_envelope_log, "Envelope");

  /* Define rotation attributes for envelope cone */
  G4RotationMatrix ecal_rotm;
  ecal_rotm.rotateX(m_XRot);
  ecal_rotm.rotateY(m_YRot);
  ecal_rotm.rotateZ(m_ZRot);

  /* Place envelope cone in simulation */
  std::string name_envelope = m_TowerLogicNamePrefix + "_envelope";

  new G4PVPlacement(G4Transform3D(ecal_rotm, G4ThreeVector(m_PlaceX, m_PlaceY, m_PlaceZ)),
                    ecal_envelope_log, name_envelope, logicWorld, 0, false, OverlapCheck());

  /* Construct single calorimeter towers */
  G4LogicalVolume* singletower[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  typedef std::map<std::string, towerposition>::iterator it_type;
  for (it_type iterator = m_TowerPositionMap.begin(); iterator != m_TowerPositionMap.end(); ++iterator)
  {
    for (int i = 0; i < 7; i++)
    {
      if (iterator->second.type == i && singletower[i] == nullptr)
      {
        singletower[i] = ConstructTower(i);
      }
    }
  }

  if (Verbosity() > 1)
  {
    std::cout << singletower << std::endl;
  }
  /* Place calorimeter towers within envelope */
  PlaceTower(ecal_envelope_log, singletower);

  return;
}

//_______________________________________________________________________
G4LogicalVolume*
PHG4ForwardEcalDetector::ConstructTower(int type)
{
  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Build logical volume for single tower, type = " << type << std::endl;
  }
  assert(type >= 0 && type <= 6);
  // This method allows construction of Type 0,1 tower (PbGl or PbW04).
  // Call a separate routine to generate Type 2 towers (PbSc)
  // Call a separate routine to generate Type 3-6 towers (E864 Pb-Scifi)

  if (type == 2) 
    return ConstructTowerType2();
  if ((type == 3) || (type == 4) || (type == 5) || (type == 6)) 
    return ConstructTowerType3_4_5_6(type);

  /* create logical volume for single tower */
  recoConsts* rc = recoConsts::instance();
  G4Material* WorldMaterial = G4Material::GetMaterial(rc->get_StringFlag("WorldMaterial"));

  G4Material* material_scintillator;
  double tower_dx = m_TowerDx[type];
  double tower_dy = m_TowerDy[type];
  double tower_dz = m_TowerDz[type];
  std::cout << "building type " << type << " towers" << std::endl;
  if (type == 0)
  {
    material_scintillator = G4Material::GetMaterial("G4_LEAD_OXIDE");
  }
  else if (type == 1)
  {
    material_scintillator = G4Material::GetMaterial("G4_PbWO4");
  }
  else
  {
    std::cout << "PHG4ForwardEcalDetector::ConstructTower invalid type = " << type << std::endl;
    material_scintillator = nullptr;
  }

  std::string single_tower_solid_name = m_TowerLogicNamePrefix + "_single_scintillator_type" + std::to_string(type);

  G4VSolid* single_tower_solid = new G4Box(single_tower_solid_name,
                                           tower_dx / 2.0,
                                           tower_dy / 2.0,
                                           tower_dz / 2.0);

  std::string single_tower_logic_name = "single_tower_logic_type" + std::to_string(type);

  G4LogicalVolume* single_tower_logic = new G4LogicalVolume(single_tower_solid,
                                                            WorldMaterial,
                                                            single_tower_logic_name,
                                                            0, 0, 0);

  std::string single_scintillator_name = "single_scintillator_type" + std::to_string(type);

  G4VSolid* solid_scintillator = new G4Box(single_scintillator_name,
                                           tower_dx / 2.0,
                                           tower_dy / 2.0,
                                           tower_dz / 2.0);

  std::string hEcal_scintillator_plate_logic_name = "hFEMC_scintillator_plate_logic_type" + std::to_string(type);

  G4LogicalVolume* logic_scint = new G4LogicalVolume(solid_scintillator,
                                                     material_scintillator,
                                                     hEcal_scintillator_plate_logic_name,
                                                     0, 0, 0);

  GetDisplayAction()->AddVolume(logic_scint, "Scintillator");

  /* place physical volumes for scintillator */

  std::string name_scintillator = m_TowerLogicNamePrefix + "_single_plate_scintillator";

  new G4PVPlacement(0, G4ThreeVector(0.0, 0.0, 0.0),
                    logic_scint,
                    name_scintillator,
                    single_tower_logic,
                    0, 0, OverlapCheck());

  GetDisplayAction()->AddVolume(single_tower_logic, "SingleTower");

  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Building logical volume for single tower done, type = " << type << std::endl;
  }

  return single_tower_logic;
}

G4LogicalVolume*
PHG4ForwardEcalDetector::ConstructTowerType2()
{
  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Build logical volume for single tower type 2..." << std::endl;
  }
  /* create logical volume for single tower */
  recoConsts* rc = recoConsts::instance();
  G4Material* WorldMaterial = G4Material::GetMaterial(rc->get_StringFlag("WorldMaterial"));

  G4VSolid* single_tower_solid = new G4Box("single_tower_solid2",
                                           m_TowerDx[2] / 2.0,
                                           m_TowerDy[2] / 2.0,
                                           m_TowerDz[2] / 2.0);

  G4LogicalVolume* single_tower_logic = new G4LogicalVolume(single_tower_solid,
                                                            WorldMaterial,
                                                            "single_tower_logic2",
                                                            0, 0, 0);

  /* create geometry volumes for scintillator and absorber plates to place inside single_tower */
  // PHENIX EMCal JGL 3/27/2016
  G4int nlayers                   = 66;
  G4double thickness_layer        = m_TowerDz[2] / (float) nlayers;
  // update layer thickness with https://doi.org/10.1016/S0168-9002(02)01954-X
  G4double thickness_cell = 5.6 * mm;
  G4double thickness_absorber     = 1.5 * mm;      // 1.5mm absorber
  G4double thickness_scintillator = 4.0 * mm;  // 4mm scintillator
  G4Material* material_scintillator = G4Material::GetMaterial("G4_POLYSTYRENE");
  G4Material* material_absorber     = G4Material::GetMaterial("G4_Pb");

  if (Verbosity())
  {
    std::cout <<" m_TowerDz[2] = "<< m_TowerDz[2]<< " thickness_layer = "<<thickness_layer<<" thickness_cell = "<<thickness_cell<<std::endl;
  }

  if (thickness_layer<=thickness_cell)
  {
    std::cout<<__PRETTY_FUNCTION__
        <<"Tower size z (m_TowerDz[2) from database is too thin. "
        <<"It does not fit the layer structure as described in https://doi.org/10.1016/S0168-9002(02)01954-X !"<<std::endl
        <<"Abort"<<std::endl;
    std::cout <<" m_TowerDz[2] = "<< m_TowerDz[2]<<" i.e. nlayers "<<nlayers<< " * thickness_layer "<<thickness_layer<<" <= thickness_cell "<<thickness_cell<<std::endl;
    exit(1);
  }
  
  
  //**********************************************************************************************
  /* create logical and geometry volumes for minitower read-out unit */
  //**********************************************************************************************
  G4VSolid* miniblock_solid         = new G4Box("miniblock_solid",
                                                m_TowerDx[2] / 2.0,
                                                m_TowerDy[2] / 2.0,
                                                thickness_cell / 2.0);
  G4LogicalVolume* miniblock_logic  = new G4LogicalVolume(miniblock_solid,
                                                          WorldMaterial,
                                                          "miniblock_logic",
                                                          0, 0, 0);
  GetDisplayAction()->AddVolume(miniblock_logic, "miniblock");
  //**********************************************************************************************
  /* create logical & geometry volumes for scintillator and absorber plates to place inside mini read-out unit */
  //**********************************************************************************************  
  G4VSolid* solid_absorber = new G4Box("single_plate_absorber_solid2",
                                       m_TowerDx[2] / 2.0,
                                       m_TowerDy[2] / 2.0,
                                       thickness_absorber / 2.0);

  G4VSolid* solid_scintillator = new G4Box("single_plate_scintillator2",
                                           m_TowerDx[2] / 2.0,
                                           m_TowerDy[2] / 2.0,
                                           thickness_scintillator / 2.0);

  G4LogicalVolume* logic_absorber = new G4LogicalVolume(solid_absorber,
                                                        material_absorber,
                                                        "single_plate_absorber_logic2",
                                                        0, 0, 0);
  m_AbsorberLogicalVolSet.insert(logic_absorber);
  G4LogicalVolume* logic_scint = new G4LogicalVolume(solid_scintillator,
                                                     material_scintillator,
                                                     "hEcal_scintillator_plate_logic2",
                                                     0, 0, 0);
  m_ScintiLogicalVolSet.insert(logic_scint);
  
  GetDisplayAction()->AddVolume(logic_absorber, "Absorber");
  GetDisplayAction()->AddVolume(logic_scint, "Scintillator");

  std::string name_absorber = m_TowerLogicNamePrefix + "_single_plate_absorber2";
  std::string name_scintillator = m_TowerLogicNamePrefix + "_single_plate_scintillator2";

  new G4PVPlacement(0, G4ThreeVector(0, 0, -thickness_scintillator/2),
                    logic_absorber,
                    name_absorber,
                    miniblock_logic,
                    0, 0, OverlapCheck());

  new G4PVPlacement(0, G4ThreeVector(0, 0, (thickness_absorber)/ 2.),
                    logic_scint,
                    name_scintillator,
                    miniblock_logic,
                    0, 0, OverlapCheck());

  /* create replica within tower */
  std::string name_tower = m_TowerLogicNamePrefix;
  new G4PVReplica(name_tower,miniblock_logic,single_tower_logic,
                      kZAxis,nlayers, thickness_layer,0);

  GetDisplayAction()->AddVolume(single_tower_logic, "SingleTower");

  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Building logical volume for single tower done." << std::endl;
  }

  return single_tower_logic;
}

G4LogicalVolume*
PHG4ForwardEcalDetector::ConstructTowerType3_4_5_6(int type)
{
  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Build logical volume for single tower type ..." << type << std::endl;
  }

  double tower_dx, tower_dy, tower_dz;
  int num_fibers_x, num_fibers_y;
  tower_dx = m_TowerDx[type];
  tower_dy = m_TowerDy[type];
  tower_dz = m_TowerDz[type];
  switch (type)
  {
  case 3:
    num_fibers_x = 10;
    num_fibers_y = 10;
    break;
  case 4:
    num_fibers_x = 9;
    num_fibers_y = 10;
    break;
  case 5:
    num_fibers_x = 10;
    num_fibers_y = 9;
    break;
  case 6:
    num_fibers_x = 9;
    num_fibers_y = 9;
    break;
  default:
    std::cout << "PHG4ForwardEcalDetector: Invalid tower type in ConstructTowerType3_4_5_6, stopping..." << std::endl;
    return nullptr;
  }

  /* create logical volume for single tower */
  recoConsts* rc = recoConsts::instance();
  G4Material* WorldMaterial = G4Material::GetMaterial(rc->get_StringFlag("WorldMaterial"));

  std::string solidName = "single_tower_solid" + std::to_string(type);
  G4VSolid* single_tower_solid = new G4Box(solidName,
                                           tower_dx / 2.0,
                                           tower_dy / 2.0,
                                           tower_dz / 2.0);

  std::string name_single_tower_logic = "single_tower_logic" + std::to_string(type);

  G4LogicalVolume* single_tower_logic = new G4LogicalVolume(single_tower_solid,
                                                            WorldMaterial,
                                                            name_single_tower_logic,
                                                            0, 0, 0);

  // Now the absorber and then the fibers:

  std::string absorberName = "single_absorber_solid" + std::to_string(type);
  G4VSolid* single_absorber_solid = new G4Box(absorberName,
                                              tower_dx / 2.0,
                                              tower_dy / 2.0,
                                              tower_dz / 2.0);

  std::string absorberLogicName = "single_absorber_logic" + std::to_string(type);
  ;
  // E864 Pb-Scifi calorimeter
  // E864 Calorimeter is 99% Pb, 1% Antimony
  G4LogicalVolume* single_absorber_logic = new G4LogicalVolume(single_absorber_solid,
                                                               G4Material::GetMaterial("E864_Absorber"),

                                                               absorberLogicName,
                                                               0, 0, 0);

  /* create geometry volumes for scintillator and place inside single_tower */
  // 1.1mm fibers

  std::string fiberName = "single_fiber_scintillator_solid" + std::to_string(type);
  G4VSolid* single_scintillator_solid = new G4Tubs(fiberName,
                                                   0.0, 0.055 * cm, (tower_dz / 2.0), 0.0, CLHEP::twopi);

  /* create logical volumes for scintillator and absorber plates to place inside single_tower */
  G4Material* material_scintillator = G4Material::GetMaterial("G4_POLYSTYRENE");

  std::string fiberLogicName = "hEcal_scintillator_fiber_logic" + std::to_string(type);
  G4LogicalVolume* single_scintillator_logic = new G4LogicalVolume(single_scintillator_solid,
                                                                   material_scintillator,
                                                                   fiberLogicName,
                                                                   0, 0, 0);
  m_AbsorberLogicalVolSet.insert(single_absorber_logic);
  m_ScintiLogicalVolSet.insert(single_scintillator_logic);
  GetDisplayAction()->AddVolume(single_absorber_logic, "Absorber");
  GetDisplayAction()->AddVolume(single_scintillator_logic, "Fiber");

  // place array of fibers inside absorber

  double fiber_unit_cell = 10.0 * cm / 47.0;
  double xpos_i = -(tower_dx / 2.0) + (fiber_unit_cell / 2.0);
  double ypos_i = -(tower_dy / 2.0) + (fiber_unit_cell / 2.0);
  double zpos_i = 0.0;

  std::string name_scintillator = m_TowerLogicNamePrefix + "_single_fiber_scintillator" + std::to_string(type);

  for (int i = 0; i < num_fibers_x; i++)
  {
    for (int j = 0; j < num_fibers_y; j++)
    {
      new G4PVPlacement(0, G4ThreeVector(xpos_i + i * fiber_unit_cell, ypos_i + j * fiber_unit_cell, zpos_i),
                        single_scintillator_logic,
                        name_scintillator,
                        single_absorber_logic,
                        0, 0, OverlapCheck());
    }
  }

  // Place the absorber inside the envelope

  std::string name_absorber = m_TowerLogicNamePrefix + "_single_absorber" + std::to_string(type);

  new G4PVPlacement(0, G4ThreeVector(0.0, 0.0, 0.0),
                    single_absorber_logic,
                    name_absorber,
                    single_tower_logic,
                    0, 0, OverlapCheck());
  GetDisplayAction()->AddVolume(single_tower_logic, "SingleTower");

  if (Verbosity() > 0)
  {
    std::cout << "PHG4ForwardEcalDetector: Building logical volume for single tower done." << std::endl;
  }

  return single_tower_logic;
}

int PHG4ForwardEcalDetector::PlaceTower(G4LogicalVolume* ecalenvelope, G4LogicalVolume* singletowerIn[7])
{
  /* Loop over all tower positions in vector and place tower */
  for (std::map<std::string, towerposition>::iterator iterator = m_TowerPositionMap.begin(); iterator != m_TowerPositionMap.end(); ++iterator)
  {
    if (Verbosity() > 0)
    {
      std::cout << "PHG4ForwardEcalDetector: Place tower " << iterator->first
                << " idx_j = " << iterator->second.idx_j << ", idx_k = " << iterator->second.idx_k
                << " at x = " << iterator->second.x << " , y = " << iterator->second.y << " , z = " << iterator->second.z << std::endl;
    }

    assert(iterator->second.type >= 0 && iterator->second.type <= 6);
    G4LogicalVolume* singletower = singletowerIn[iterator->second.type];
    int copyno = (iterator->second.idx_j << 16) + iterator->second.idx_k;

    G4PVPlacement* tower_placement =
        new G4PVPlacement(0, G4ThreeVector(iterator->second.x, iterator->second.y, iterator->second.z),
                          singletower,
                          iterator->first,
                          ecalenvelope,
                          0, copyno, OverlapCheck());

    m_GdmlConfig->exclude_physical_vol(tower_placement);
  }

  return 0;
}

int PHG4ForwardEcalDetector::ParseParametersFromTable()
{
  /* Open the datafile, if it won't open return an error */
  std::ifstream istream_mapping;
  istream_mapping.open(m_Params->get_string_param("mapping_file"));
  if (!istream_mapping.is_open())
  {
    std::cout << "ERROR in PHG4ForwardEcalDetector: Failed to open mapping file " << m_Params->get_string_param("mapping_file") << std::endl;
    gSystem->Exit(1);
  }

  /* loop over lines in file */
  std::string line_mapping;
  while (getline(istream_mapping, line_mapping))
  {
    /* Skip lines starting with / including a '#' */
    if (line_mapping.find("#") != std::string::npos)
    {
      if (Verbosity() > 0)
      {
        std::cout << "PHG4ForwardEcalDetector: SKIPPING line in mapping file: " << line_mapping << std::endl;
      }
      continue;
    }

    std::istringstream iss(line_mapping);

    /* If line starts with keyword Tower, add to tower positions */
    if (line_mapping.find("Tower ") != std::string::npos)
    {
      unsigned idx_j, idx_k, idx_l;
      G4double pos_x, pos_y, pos_z;
      G4double size_x, size_y, size_z;
      G4double rot_x, rot_y, rot_z;
      int type;
      std::string dummys;

      /* read string- break if error */
      if (!(iss >> dummys >> type >> idx_j >> idx_k >> idx_l >> pos_x >> pos_y >> pos_z >> size_x >> size_y >> size_z >> rot_x >> rot_y >> rot_z))
      {
        std::cout << "ERROR in PHG4ForwardEcalDetector: Failed to read line in mapping file " << m_Params->get_string_param("mapping_file") << std::endl;
        gSystem->Exit(1);
      }

      /* Construct unique name for tower */
      /* Mapping file uses cm, this class uses mm for length */
      std::ostringstream towername;
      towername << m_TowerLogicNamePrefix << "_t_" << type << "_j_" << idx_j << "_k_" << idx_k;
      /* Add Geant4 units */
      pos_x = pos_x * cm;
      pos_y = pos_y * cm;
      pos_z = pos_z * cm;

      /* insert tower into tower map */
      towerposition tower_new;
      tower_new.x = pos_x;
      tower_new.y = pos_y;
      tower_new.z = pos_z;
      tower_new.idx_j = idx_j;
      tower_new.idx_k = idx_k;
      tower_new.type = type;
      m_TowerPositionMap.insert(std::make_pair(towername.str(), tower_new));
    }
    else
    {
      /* If this line is not a comment and not a tower, save parameter as string / value. */
      std::string parname;
      double parval;

      /* read string- break if error */
      if (!(iss >> parname >> parval))
      {
        std::cout << "ERROR in PHG4ForwardEcalDetector: Failed to read line in mapping file " << m_Params->get_string_param("mapping_file") << std::endl;
        gSystem->Exit(1);
      }

      m_GlobalParameterMap.insert(std::make_pair(parname, parval));
    }
  }
  /* Update member variables for global parameters based on parsed parameter file */
  std::map<std::string, double>::iterator parit;
  std::ostringstream twr;
  for (int i = 0; i < 7; i++)
  {
    twr.str("");
    twr << "Gtower" << i << "_dx";
    parit = m_GlobalParameterMap.find(twr.str());
    m_TowerDx[i] = parit->second * cm;
    twr.str("");
    twr << "Gtower" << i << "_dy";
    parit = m_GlobalParameterMap.find(twr.str());
    m_TowerDy[i] = parit->second * cm;
    twr.str("");
    twr << "Gtower" << i << "_dz";
    parit = m_GlobalParameterMap.find(twr.str());
    m_TowerDz[i] = parit->second * cm;
  }
  std::ostringstream rad;
  for (int i = 0; i < 2; i++)
  {
    int index = i + 1;
    rad.str("");
    rad << "Gr" << index << "_inner";
    parit = m_GlobalParameterMap.find(rad.str());
    if (parit != m_GlobalParameterMap.end())
    {
      m_RMin[i] = parit->second * cm;
    }
    rad.str("");
    rad << "Gr" << index << "_outer";
    parit = m_GlobalParameterMap.find(rad.str());
    if (parit != m_GlobalParameterMap.end())
    {
      m_RMax[i] = parit->second * cm;
    }
  }
  parit = m_GlobalParameterMap.find("Gdz");
  if (parit != m_GlobalParameterMap.end())
  {
    m_dZ = parit->second * cm;
  }
  parit = m_GlobalParameterMap.find("Gx0");
  if (parit != m_GlobalParameterMap.end())
  {
    m_PlaceX = parit->second * cm;
  }
  parit = m_GlobalParameterMap.find("Gy0");
  if (parit != m_GlobalParameterMap.end())
  {
    m_PlaceY = parit->second * cm;
  }
  parit = m_GlobalParameterMap.find("Gz0");
  if (parit != m_GlobalParameterMap.end())
  {
    m_PlaceZ = parit->second * cm;
  }
  parit = m_GlobalParameterMap.find("Grot_x");
  if (parit != m_GlobalParameterMap.end())
  {
    m_XRot = parit->second;
  }
  parit = m_GlobalParameterMap.find("Grot_y");
  if (parit != m_GlobalParameterMap.end())
  {
    m_YRot = parit->second;
  }
  parit = m_GlobalParameterMap.find("Grot_z");
  if (parit != m_GlobalParameterMap.end())
  {
    m_ZRot = parit->second;
  }
  parit = m_GlobalParameterMap.find("tower_type");
  if (parit != m_GlobalParameterMap.end())
  {
    m_TowerType = parit->second;
  }
  
  parit = m_GlobalParameterMap.find("xoffset");
  if (parit != m_GlobalParameterMap.end())
    m_Params->set_double_param("xoffset", parit->second);  

  parit = m_GlobalParameterMap.find("yoffset");
  if (parit != m_GlobalParameterMap.end())
    m_Params->set_double_param("yoffset", parit->second);  

  
  return 0;
}

void PHG4ForwardEcalDetector::SetTowerDimensions(double dx, double dy, double dz, int type)
{
  assert(type >= 0 && type <= 6);
  m_TowerDx[type] = dx;
  m_TowerDy[type] = dy;
  m_TowerDz[type] = dz;
}
