// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_art.h: Complete 149/149 (100%) framed retail Goodies concept art
// plates staged in a two-tier museum salon hang across the Concept Art Gallery (75 plates)
// and Early Designs Vault (74 plates). Pure C++14 manifest, aspect-preserving 256px upload
// sizing (~37 MiB total VRAM across all 149 simultaneously resident plates, zero doorway
// pop-in), wing-scoped 2D/3D plaque targeting, and host verification.
#ifndef CE_MUSEUM_ART_H
#define CE_MUSEUM_ART_H

#include "ce_museum_tour.h"
#include <cmath>

namespace CeMuseumArt {

constexpr unsigned kMaxTexEdge = 256u;
// All 149/149 retail GT_IMAGE Goodies stay simultaneously resident (149 * 256^2 * 4 <= 37.25 MiB).
enum { kMaxResidentCanvases = 149, kManifestCount = 149 };

struct CanvasSpec {
    int goodieId;             // retail GT_IMAGE goodie ID from FEPGoodies.cpp
    CeMuseumTour::Wing wing;
    const char *retailPath;   // lookup key in player's installed Goodies textures
    const char *title;
    const char *category;
    const char *caption;
    float localX, localY;     // wall-mounted centre in visitor-local metres
    float centreZ;            // positive upward height of canvas centre
    float maxWidth, maxHeight; // frame opening bounds in metres
    float normalX, normalY;   // unit normal pointing into the gallery room
};

// Strictly factual curator titles & captions (zero internal filenames or build jargon).
static const CanvasSpec kManifest[kManifestCount] = {
    // =========================================================================
    // Concept Art Gallery (Southeast: x [16, 40], y [10, 28]) — 75 plates
    // =========================================================================

    { 71, CeMuseumTour::kWingConceptArt, "goodies\\ca_be_aquila\\ca_be_final01.tga",
     "Battle Engine Aquila - Study I", "Production Concept Art",
     "Final walker-mode silhouette and Forseti blue-steel livery study.",
     18.40f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 72, CeMuseumTour::kWingConceptArt, "goodies\\ca_be_aquila\\ca_be_final02.tga",
     "Battle Engine Aquila - Study II", "Production Concept Art",
     "Flight-mode wing geometry and thruster alignment painting.",
     18.40f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  0, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_hawk.tga",
     "Hawk - Forseti Pilot", "Character Concept Art",
     "Lead Battle Engine pilot portrait and Commonwealth flight suit study.",
     20.80f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  1, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_tatianna.tga",
     "Tatianna - Forseti Officer", "Character Concept Art",
     "Commonwealth fleet intelligence officer portrait and uniform study.",
     20.80f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  2, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_kramer.tga",
     "Kramer - Forseti Wingman", "Character Concept Art",
     "Veteran Forseti interceptor ace character and flight helmet study.",
     23.20f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  3, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_lorenzo.tga",
     "Lorenzo - Forseti Tactician", "Character Concept Art",
     "Commonwealth operations officer portrait and field dress sheet.",
     23.20f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  4, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_tara.tga",
     "Tara - Forseti Commander", "Character Concept Art",
     "Forseti command officer study detailing insignia and field gear.",
     25.60f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  5, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_billy.tga",
     "Billy - Forseti Specialist", "Character Concept Art",
     "Commonwealth technical specialist portrait and equipment sheet.",
     25.60f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  6, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_carver.tga",
     "Carver - Rogue Ace", "Character Concept Art",
     "Rival ace pilot portrait with custom flight jacket and visor design.",
     28.00f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {  7, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_characters\\ca_mc_surt.tga",
     "General Surt", "Character Concept Art",
     "Supreme Muspell Dominion commander portrait and ceremonial armour.",
     28.00f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 12, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_characters\\ca_fc_trooper01.tga",
     "Forseti Infantry Trooper", "Character Concept Art",
     "Frontline Commonwealth marine armour, visor, and pulse rifle study.",
     30.40f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 33, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_characters\\ca_mc_grunt.tga",
     "Muspell Grunt Trooper", "Character Concept Art",
     "Rank-and-file Dominion infantry armour and respirator mask study.",
     30.40f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 34, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_characters\\ca_mc_firebreather.tga",
     "Muspell Firebreather", "Character Concept Art",
     "Heavy Dominion flame trooper wearing insulated volcanic combat gear.",
     32.80f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 35, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_characters\\ca_mc_commando.tga",
     "Muspell Commando", "Character Concept Art",
     "Elite Dominion infiltration trooper portrait and stealth plating study.",
     32.80f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 60, CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_sub.tga",
     "Muspell Attack Submarine", "Boss Design Art",
     "Submersible Dominion naval dreadnought and missile bay concept.",
     35.20f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 61, CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_hivev02.tga",
     "Hive Command Complex", "Boss Design Art",
     "Fortified Dominion staging complex and orbital-ridge fortress study.",
     35.20f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 62, CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_gill-m.tga",
     "Kraken Super-Weapon", "Boss Design Art",
     "Colossal maritime war machine surfacing from arctic storm waters.",
     37.60f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 63, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_carvers_plane.tga",
     "Carver Strike Fighter", "Muspell Aviation Art",
     "Custom swept-wing interceptor flown by the rival Dominion ace.",
     37.60f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 64, CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_fenrir.tga",
     "Fenrir Command Fortress", "Boss Design Art",
     "Fortified Muspell super-structure and aerial command tower painting.",
     18.40f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    { 24, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_venturer.tga",
     "F.S. Venturer Flagship", "Forseti Naval Art",
     "Command flagship and fleet flight-deck exterior concept painting.",
     18.40f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {120, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\CA_FU_ forseti forces.tga",
     "Forseti Forces Lineup", "Forseti Armour Art",
     "Combined-arms visual guide unifying Commonwealth vehicles and aircraft.",
     20.80f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {121, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_a15_ground_attack.tga",
     "A-15B Ground Attack", "Forseti Aviation Art",
     "Armoured close air support airframe with underwing munitions.",
     20.80f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {122, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_av6_rhino_apc.tga",
     "AV-6 Rhino APC", "Forseti Armour Art",
     "Armoured personnel transport study with troop bay detailing.",
     23.20f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {129, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_f24.tga",
     "F-24 Fighter", "Forseti Aviation Art",
     "Frontline Forseti twin-engine air superiority interceptor sheet.",
     23.20f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {130, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_heavy_bomber.tga",
     "Forseti Heavy Bomber", "Forseti Aviation Art",
     "High-altitude strategic strike bomber profile and wing assembly.",
     25.60f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {131, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_heavy_lifter.tga",
     "Forseti Heavy Lifter", "Forseti Transport Art",
     "VTOL logistics transport built to deploy armour into combat zones.",
     25.60f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {132, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_sat_launcher.tga",
     "Forseti SAT Launcher", "Forseti Armour Art",
     "Tracked surface-to-air tactical missile platform concept sheet.",
     28.00f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {134, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_tank_av14.tga",
     "AV-14 Tank Study", "Forseti Armour Art",
     "Medium tracked tank turret and suspension proportion study.",
     28.00f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {135, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_tank_av18.tga",
     "AV-18 Heavy Tank Study", "Forseti Armour Art",
     "Reinforced heavy Commonwealth assault tank elevation drawing.",
     30.40f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {136, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_tank_m1.tga",
     "M1 Tank Concept", "Forseti Armour Art",
     "Early frontline Forseti battle tank silhouette and cannon study.",
     30.40f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {137, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_tank_m112.tga",
     "M112 Armoured Vehicle", "Forseti Armour Art",
     "Wheeled reconnaissance and fire-support vehicle design sheet.",
     32.80f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {138, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_tanks.tga",
     "Forseti Tank Variants", "Forseti Armour Art",
     "Comparative turret configurations across the Forseti armoured corps.",
     32.80f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {139, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_truck.tga",
     "Forseti Supply Truck", "Forseti Armour Art",
     "Six-wheel Commonwealth logistics transport and cargo frame study.",
     35.20f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {140, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_wildcard_plane.tga",
     "Wildcard Strike Aircraft", "Forseti Aviation Art",
     "Specialized twin-tail Forseti strike fighter concept painting.",
     35.20f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {141, CeMuseumTour::kWingConceptArt, "goodies\\ca_f_units\\ca_fu_xf24-venom_fighter.tga",
     "XF-24 Venom Fighter", "Forseti Aviation Art",
     "High-speed swept-wing Forseti interceptor design sheet.",
     37.60f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {152, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu__crab_mech.tga",
     "Muspell Gun Crab", "Muspell Armour Art",
     "Heavy quadrupedal Dominion assault walker study.",
     37.60f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {153, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_apc.tga",
     "Muspell Armoured APC", "Muspell Armour Art",
     "Tracked Dominion troop transport with sloped iron glacis armour.",
     22.74f, 15.35f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {154, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_artillery.tga",
     "Muspell Siege Artillery", "Muspell Armour Art",
     "Mobile long-range bombardment carriage in Dominion livery.",
     22.74f, 15.35f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {155, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_asx9_tactical_advanced.tga",
     "ASX-9 Tactical Fighter", "Muspell Aviation Art",
     "Advanced Dominion air superiority interceptor colour study.",
     22.74f, 17.70f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {156, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_bomber.tga",
     "Muspell Strategic Bomber", "Muspell Aviation Art",
     "Swept-wing heavy Dominion strike aircraft concept painting.",
     22.74f, 17.70f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {166, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_ground_attack.tga",
     "Muspell Ground Attack", "Muspell Aviation Art",
     "Low-altitude Dominion strike airframe with forward gun pods.",
     22.74f, 20.05f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {167, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_heavy_lifter.tga",
     "Muspell Heavy Lifter", "Muspell Aviation Art",
     "Armoured Dominion vertical-lift dropship and engine pod sheet.",
     22.74f, 20.05f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {168, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_interceptor.tga",
     "Muspell Interceptor", "Muspell Aviation Art",
     "Agile frontline Dominion dogfighter profile and wing markings.",
     23.26f, 15.35f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {169, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_mech.tga",
     "Muspell Gun Walker", "Muspell Armour Art",
     "Angular iron-and-ochre Dominion bipedal mech study.",
     23.26f, 15.35f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {170, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_mobile_outpost.tga",
     "Muspell Mobile Outpost", "Muspell Armour Art",
     "Self-propelled Dominion field command and communications platform.",
     23.26f, 17.70f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {171, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_rifle_mech.tga",
     "Muspell Rifle Mech", "Muspell Armour Art",
     "Medium bipedal Dominion mech armed with twin autocannon pods.",
     23.26f, 17.70f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {172, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_rocket_launcher.tga",
     "Muspell Rocket Launcher", "Muspell Armour Art",
     "Heavy salvo missile carriage designed to saturate island defences.",
     23.26f, 20.05f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {173, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_tank.tga",
     "Muspell Main Battle Tank", "Muspell Armour Art",
     "Heavy Dominion tracked armour silhouette and turret geometry.",
     23.26f, 20.05f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {174, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_units\\ca_mu_truck.tga",
     "Muspell Supply Truck", "Muspell Armour Art",
     "Armoured Dominion logistics haulage vehicle concept sheet.",
     27.74f, 15.35f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 92, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_briefing_room.tga",
     "Forseti Briefing Room", "Environment Concept Art",
     "Tactical operations chamber with central holographic mission table.",
     27.74f, 15.35f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 93, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_bunk_room.tga",
     "Forseti Crew Quarters", "Environment Concept Art",
     "Shipboard bunk room interior lighting and bunk framing study.",
     27.74f, 17.70f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 94, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_cell.tga",
     "Forseti Holding Cell", "Environment Concept Art",
     "Commonwealth security detention bay interior architecture study.",
     27.74f, 17.70f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 95, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_command_room_interior.tga",
     "Forseti Command Room Interior", "Environment Concept Art",
     "High-command bridge interior with tactical screens and steel bulkheads.",
     27.74f, 20.05f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 96, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_docks_exterior.tga",
     "Forseti Naval Docks - Exterior", "Environment Concept Art",
     "Coastal drydock gantry cranes and armoured harbour seawalls.",
     27.74f, 20.05f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 97, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_docks_interior.tga",
     "Forseti Naval Docks - Interior", "Environment Concept Art",
     "Covered maritime staging bay and ship refit interior study.",
     28.26f, 15.35f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    { 98, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_fighter_cockpit.tga",
     "Forseti Fighter Cockpit", "Environment Concept Art",
     "First-person interceptor canopy framing and flight instrument console.",
     28.26f, 15.35f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    { 99, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_hanger_interior.tga",
     "Forseti Fleet Hangar", "Environment Concept Art",
     "Interior staging deck and Battle Engine launch bay study.",
     28.26f, 17.70f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {100, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_lifter_cockpit.tga",
     "Forseti Lifter Cockpit", "Environment Concept Art",
     "Heavy vertical-lift transport flight deck and overhead control switches.",
     28.26f, 17.70f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {101, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_loader_mech.tga",
     "Forseti Dock Loader Mech", "Environment Concept Art",
     "Industrial hangar utility walker moving ordnance across the flight deck.",
     28.26f, 20.05f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {102, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_marshall_bridge.tga",
     "F.S. Marshall Bridge", "Environment Concept Art",
     "Command deck overlook and navigation console aboard F.S. Marshall.",
     28.26f, 20.05f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {103, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_research_compound.tga",
     "Forseti Research Compound", "Environment Concept Art",
     "Island laboratory complex where Battle Engine systems are refined.",
     32.74f, 15.35f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {104, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_f_venturer_bridge.tga",
     "F.S. Venturer Bridge", "Environment Concept Art",
     "Flagship command bridge panorama overlooking the fleet launch deck.",
     32.74f, 15.35f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {105, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_m_cell.tga",
     "Muspell Detention Cell", "Environment Concept Art",
     "Severe iron-walled Dominion holding bay interior mood painting.",
     32.74f, 17.70f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {106, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_m_fenrir_bridge01.tga",
     "Fenrir Command Bridge - Study I", "Environment Concept Art",
     "Monumental dark-steel bridge gallery inside the Fenrir stronghold.",
     32.74f, 17.70f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {107, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_m_fenrir_bridge02.tga",
     "Fenrir Command Bridge - Study II", "Environment Concept Art",
     "Command overlook and tactical dais lighting inside Fenrir.",
     32.74f, 20.05f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {108, CeMuseumTour::kWingConceptArt, "goodies\\ca_cutscene_locations\\ca_cl_m_staging_area.tga",
     "Muspell Staging Area", "Environment Concept Art",
     "Industrial Dominion assembly floor preparing walkers for deployment.",
     32.74f, 20.05f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {142, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_architecture\\ca_ma_airfighterfactory.tga",
     "Muspell Airfighter Factory", "Muspell Architecture",
     "Dominion aircraft assembly foundry with iron blast doors.",
     33.26f, 15.35f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {143, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_architecture\\ca_ma_barracks.tga",
     "Muspell Garrison Barracks", "Muspell Architecture",
     "Armoured Dominion troop housing and iron watchtower block.",
     33.26f, 15.35f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {144, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_architecture\\ca_ma_mech_factory.tga",
     "Muspell Mech Factory", "Muspell Architecture",
     "Heavy bipedal walker fabrication plant and slag furnace chimneys.",
     33.26f, 17.70f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {145, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_architecture\\ca_ma_muspell architecture.tga",
     "Muspell Architecture Guide", "Muspell Architecture",
     "Angular volcanic-iron structural grammar defining Dominion bases.",
     33.26f, 17.70f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {146, CeMuseumTour::kWingConceptArt, "goodies\\ca_m_architecture\\ca_ma_tankfactory.tga",
     "Muspell Tank Factory", "Muspell Architecture",
     "Reinforced armoured vehicle foundry and rampway staging depot.",
     33.26f, 20.05f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    { 74, CeMuseumTour::kWingConceptArt, "goodies\\ashley.tga",
     "Studio Portrait - Ashley", "Studio Archive",
     "Commemorative studio character sketch preserved in the archives.",
     33.26f, 20.05f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    { 58, CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_thunderhead.tga",
     "Thunderhead Super-Bomber", "Boss Design Art",
     "Monumental airborne command fortress painted over storm clouds.",
     39.68f, 20.80f, 1.90f, 1.95f, 1.50f, -1.0f, 0.0f},
    { 59, CeMuseumTour::kWingConceptArt, "goodies\\ca_bosses\\ca_boss_warspite.tga",
     "Warspite Assault Platform", "Boss Design Art",
     "Heavy mobile bastion and multi-turret fortress elevation study.",
     39.68f, 24.20f, 1.90f, 1.95f, 1.50f, -1.0f, 0.0f},
    { 73, CeMuseumTour::kWingConceptArt, "goodies\\ca_be_aquila\\ca_bea_battle_pic.tga",
     "Coastal Assault Panorama", "Key Production Illustration",
     "Combined-arms beachhead action featuring the Aquila over sea and armour.",
     39.68f, 22.50f, 4.05f, 2.20f, 1.65f, -1.0f, 0.0f},

    // =========================================================================
    // Early Designs Vault (Southwest: x [-40, -16], y [10, 28]) — 74 plates
    // =========================================================================

    { 78, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v1.tga",
     "Aquila Design Study v1", "Early Walker Revision",
     "Initial bipedal war-machine exploration with angular cockpit canopy.",
     -19.20f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 79, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v2_bluprint01.tga",
     "Aquila v2 Blueprint - Elevation", "Technical Blueprint",
     "Orthographic mechanical drawing from the second Battle Engine revision.",
     -19.20f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 80, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v2_bluprint02.tga",
     "Aquila v2 Blueprint - Armament", "Technical Blueprint",
     "Weapon pod articulation and thruster placement sheet for revision two.",
     -21.70f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 81, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v2_pg01.tga",
     "Aquila v2 Sketch Sheet I", "Early Walker Revision",
     "Early posture and landing-gear articulation studies for revision two.",
     -21.70f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 82, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v2_pg02.tga",
     "Aquila v2 Sketch Sheet II", "Early Walker Revision",
     "Flight-to-walker folding wing hinge explorations from pre-production.",
     -24.20f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 83, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v3.tga",
     "Aquila Design Study v3", "Intermediate Revision",
     "Third-pass chassis balancing walker leg stance with aircraft wings.",
     -24.20f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 84, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v4&5.tga",
     "Aquila Design Studies v4 and v5", "Iterative Studies",
     "Side-by-side silhouette revisions refining dorsal intakes and cockpit.",
     -26.70f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 85, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v6.tga",
     "Aquila Design Study v6", "Late Pre-Production",
     "Near-final transformation frame establishing the shipped proportions.",
     -26.70f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 86, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_be_aquila\\ca_be_v7.tga",
     "Aquila Design Study v7", "Final Pre-Production",
     "Final silhouette reference completed prior to game-ready modelling.",
     -29.20f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    { 13, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_characters\\ca_fc_early_trooper.tga",
     "Early Forseti Trooper", "Character Archive",
     "Early infantry combat armour and visor exploration for Forseti troops.",
     -29.20f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {109, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings01.tga",
     "Forseti Architecture - Study I", "Forseti Architecture",
     "Modular coastal garrison and radar tower elevation studies.",
     -31.70f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {110, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings02.tga",
     "Forseti Architecture - Study II", "Forseti Architecture",
     "Armoured bunker doorway and reinforced concrete seawall details.",
     -31.70f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {111, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings03.tga",
     "Forseti Architecture - Study III", "Forseti Architecture",
     "Commonwealth communications spire and command shelter silhouettes.",
     -34.20f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {112, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings04.tga",
     "Forseti Architecture - Study IV", "Forseti Architecture",
     "Harbour crane gantry and coastal supply depot architecture.",
     -34.20f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {113, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings05.tga",
     "Forseti Architecture - Study V", "Forseti Architecture",
     "Island hangar dome and defensive wall junction studies.",
     -36.70f, 27.68f, 1.90f, 1.85f, 1.45f, 0.0f, -1.0f},
    {114, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings06.tga",
     "Forseti Architecture - Study VI", "Forseti Architecture",
     "Civilian harbour frontage and stepped limestone seawall concept.",
     -36.70f, 27.68f, 4.05f, 1.85f, 1.45f, 0.0f, -1.0f},
    {115, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings07.tga",
     "Forseti Architecture - Study VII", "Forseti Architecture",
     "Power relay station and coastal generator complex elevations.",
     -19.20f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {116, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_architecture\\ca_fa_f_buildings08.tga",
     "Forseti Architecture - Study VIII", "Forseti Architecture",
     "Fortified lighthouse watchtower and cliffside bunker ensemble.",
     -19.20f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {117, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_characters\\ca_fc_characters.tga",
     "Forseti Characters Lineup", "Character Archive",
     "Comparative height and silhouette lineup of the Forseti cast.",
     -21.70f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {118, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_characters\\ca_fc_general.tga",
     "Forseti General Study", "Character Archive",
     "Senior Commonwealth commanding officer uniform and posture study.",
     -21.70f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {119, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_characters\\ca_fc_trooper02.tga",
     "Forseti Trooper - Study II", "Character Archive",
     "Alternate helmet optics and shoulder plate study for marine squads.",
     -24.20f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {123, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft01.tga",
     "Early Forseti Aircraft I", "Unshipped Concept",
     "Pre-production aerial hull exploration predating the shipped fighters.",
     -24.20f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {124, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft02.tga",
     "Early Forseti Aircraft II", "Unshipped Concept",
     "Alternate twin-boom Forseti interceptor hull explored early on.",
     -26.70f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {125, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft03.tga",
     "Early Forseti Aircraft III", "Unshipped Concept",
     "Unshipped Commonwealth ground-attack wing planform study.",
     -26.70f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {126, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft04.tga",
     "Early Forseti Aircraft IV", "Unshipped Concept",
     "Swept-wing coastal patrol craft sketched during pre-production.",
     -29.20f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {127, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft05.tga",
     "Early Forseti Aircraft V", "Unshipped Concept",
     "Heavy tilt-thruster Commonwealth airframe concept sheet.",
     -29.20f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {128, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_early_craft06.tga",
     "Early Forseti Aircraft VI", "Unshipped Concept",
     "Compact delta-wing scout craft exploration from early archives.",
     -31.70f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {133, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_units\\ca_fu_style.tga",
     "Forseti Visual Style Guide", "Pre-Production Study",
     "Material, colour palette, and panel-line language for Forseti forces.",
     -31.70f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {147, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_characters\\ca_mc_4eyes.tga",
     "Muspell Four-Eyes Officer", "Character Archive",
     "Early multi-optic helmet and command suit study for Dominion scouts.",
     -34.20f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {148, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_characters\\ca_mc_early_grunt.tga",
     "Early Muspell Grunt", "Character Archive",
     "Pre-production Dominion infantry helmet and armour plate sketches.",
     -34.20f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {149, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_characters\\ca_mc_early_surt.tga",
     "Early General Surt Study", "Character Archive",
     "Pre-production command uniform and silhouette study for General Surt.",
     -36.70f, 10.32f, 1.90f, 1.85f, 1.45f, 0.0f, 1.0f},
    {150, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_characters\\ca_mc_hunter.tga",
     "Muspell Hunter Study", "Character Archive",
     "Elite Dominion tracker concept wearing angular iron recon plating.",
     -36.70f, 10.32f, 4.05f, 1.85f, 1.45f, 0.0f, 1.0f},
    {151, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_characters\\ca_mc_muspell characters.tga",
     "Muspell Characters Lineup", "Character Archive",
     "Comparative cast sheet defining Dominion uniforms and silhouettes.",
     -22.74f, 15.35f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {157, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_carvers_plane.tga",
     "Carver Interceptor - Archive", "Pre-Production Study",
     "Archival profile sheet for Carver's bespoke Dominion airframe.",
     -22.74f, 15.35f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {158, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_craft05.tga",
     "Muspell Craft Study V", "Unshipped Concept",
     "Angular heavy gunship hull explored for Dominion air wings.",
     -22.74f, 17.70f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {159, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_craft01.tga",
     "Early Muspell Craft I", "Unshipped Concept",
     "Early Dominion strike craft silhouette from pre-production archives.",
     -22.74f, 17.70f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {160, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_craft02.tga",
     "Early Muspell Craft II", "Unshipped Concept",
     "Angular forward-swept Dominion fighter exploration.",
     -22.74f, 20.05f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {161, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_craft03.tga",
     "Early Muspell Craft III", "Unshipped Concept",
     "Twin-hull Dominion ground-attack concept from early development.",
     -22.74f, 20.05f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {162, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_craft04.tga",
     "Early Muspell Craft IV", "Unshipped Concept",
     "Armoured Dominion interceptor wing geometry sketch sheet.",
     -23.26f, 15.35f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {163, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_forces.tga",
     "Early Dominion Forces I", "Pre-Production Study",
     "Comparative silhouette chart establishing the Muspell mechanical style.",
     -23.26f, 15.35f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {164, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_forces01.tga",
     "Early Dominion Forces II", "Pre-Production Study",
     "Second-pass scale chart uniting Dominion mechs and tracked armour.",
     -23.26f, 17.70f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {165, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_units\\ca_mu_early_sam site.tga",
     "Early Muspell SAM Site", "Pre-Production Study",
     "Fortified surface-to-air missile battery emplacement concept.",
     -23.26f, 17.70f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {175, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_graphics01.tga",
     "Muspell Graphics - Plate I", "Faction Typography",
     "Geometric Dominion stencil and bulkhead graphic motif studies.",
     -23.26f, 20.05f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {176, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_graphics02.tga",
     "Muspell Graphics - Plate II", "Faction Typography",
     "Industrial warning chevron and foundry emblem explorations.",
     -23.26f, 20.05f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {177, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_graphics03.tga",
     "Muspell Graphics - Plate III", "Faction Typography",
     "Angular crimson-and-ochre Dominion livery striping layouts.",
     -27.74f, 15.35f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {178, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_graphics04.tga",
     "Muspell Graphics - Plate IV", "Faction Typography",
     "Fortress gate banner and steel-plate signage typography.",
     -27.74f, 15.35f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {179, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_insignia 01.tga",
     "Muspell Insignia - Plate I", "Faction Typography",
     "Primary Muspell Dominion crest geometry and shield studies.",
     -27.74f, 17.70f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {180, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_insigina 02.tga",
     "Muspell Insignia - Plate II", "Faction Typography",
     "Regimental battalion marks and air-wing tail emblems.",
     -27.74f, 17.70f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {181, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_insigina 03.tga",
     "Muspell Insignia - Plate III", "Faction Typography",
     "Armoured division badges and command rank heraldry.",
     -27.74f, 20.05f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {182, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_numbers01.tga",
     "Muspell Numerals - Plate I", "Faction Typography",
     "Angular stencil numeral system for Dominion hulls and hangars.",
     -27.74f, 20.05f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {183, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_numbers02.tga",
     "Muspell Numerals - Plate II", "Faction Typography",
     "High-contrast tactical designation digits for mechs and bombers.",
     -28.26f, 15.35f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {184, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_numbers03.tga",
     "Muspell Numerals - Plate III", "Faction Typography",
     "Foundry bay numbering and ordnance crate stencil designs.",
     -28.26f, 15.35f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {185, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_numbers04.tga",
     "Muspell Numerals - Plate IV", "Faction Typography",
     "Heavy bastion sector codes and bulkhead serial typography.",
     -28.26f, 17.70f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {186, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_ss00.tga",
     "Muspell Script - Plate I", "Faction Typography",
     "Constructed Dominion lettering and angular glyph proportions.",
     -28.26f, 17.70f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {187, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_ss01.tga",
     "Muspell Script - Plate II", "Faction Typography",
     "Dominion tactical display glyphs and command console signage.",
     -28.26f, 20.05f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {188, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_ss02.tga",
     "Muspell Script - Plate III", "Faction Typography",
     "Industrial warning placard lettering for Muspell facilities.",
     -28.26f, 20.05f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {189, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_ss03.tga",
     "Muspell Script - Plate IV", "Faction Typography",
     "Stencilled hangar deck directives in Dominion script.",
     -32.74f, 15.35f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {190, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_ss04.tga",
     "Muspell Script - Plate V", "Faction Typography",
     "Fortress bulkhead header script and angular glyph grid.",
     -32.74f, 15.35f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {191, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_m_language\\ca_ml_ss05.tga",
     "Muspell Script - Plate VI", "Faction Typography",
     "Complete Dominion graphic character set for world signage.",
     -32.74f, 17.70f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {192, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_01.tga",
     "Forseti Language - Plate I", "Faction Typography",
     "Clean Commonwealth stencil typography for warships and hangars.",
     -32.74f, 17.70f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {193, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_02.tga",
     "Forseti Language - Plate II", "Faction Typography",
     "Forseti air-wing fuselage markings and squadron numerals.",
     -32.74f, 20.05f, 1.90f, 1.85f, 1.45f, 1.0f, 0.0f},
    {194, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_03.tga",
     "Forseti Language - Plate III", "Faction Typography",
     "Commonwealth naval hull codes and flight-deck striping.",
     -32.74f, 20.05f, 4.05f, 1.85f, 1.45f, 1.0f, 0.0f},
    {195, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_04.tga",
     "Forseti Language - Plate IV", "Faction Typography",
     "Tactical HUD badge motifs and Commonwealth command crests.",
     -33.26f, 15.35f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {196, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_05.tga",
     "Forseti Language - Plate V", "Faction Typography",
     "Harbour dockside signage and supply depot identification plates.",
     -33.26f, 15.35f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {197, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_06.tga",
     "Forseti Language - Plate VI", "Faction Typography",
     "Forseti emblem proportions and cobalt-blue colour standards.",
     -33.26f, 17.70f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {198, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_07.tga",
     "Forseti Language - Plate VII", "Faction Typography",
     "Armoured corps turret numbers and platoon insignia sheet.",
     -33.26f, 17.70f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    {199, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_08.tga",
     "Forseti Language - Plate VIII", "Faction Typography",
     "Commonwealth airfield runway codes and hangar bay lettering.",
     -33.26f, 20.05f, 1.90f, 1.85f, 1.45f, -1.0f, 0.0f},
    {200, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_f_language\\ca_fl_09.tga",
     "Forseti Language - Plate IX", "Faction Typography",
     "Unified Commonwealth insignia and fleet identification manual.",
     -33.26f, 20.05f, 4.05f, 1.85f, 1.45f, -1.0f, 0.0f},
    { 65, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_sentinel_01.tga",
     "Sentinel Fortress - Study I", "Boss Design Art",
     "Monolithic Muspell Sentinel stronghold and turret layout study.",
     -39.68f, 15.40f, 1.90f, 1.95f, 1.50f, 1.0f, 0.0f},
    { 87, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_sentinel_02.tga",
     "Sentinel Fortress - Study II", "Boss Archive",
     "Alternate Sentinel citadel elevation exploring heavy gun batteries.",
     -39.68f, 15.40f, 4.05f, 1.95f, 1.50f, 1.0f, 0.0f},
    { 88, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_sentinel_03.tga",
     "Sentinel Fortress - Study III", "Boss Archive",
     "Upper command spire and armoured buttress proportions for Sentinel.",
     -39.68f, 19.00f, 1.90f, 1.95f, 1.50f, 1.0f, 0.0f},
    { 89, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_sentinel_04.tga",
     "Sentinel Fortress - Study IV", "Boss Archive",
     "Finalized Sentinel defensive perimeter and cannon arc composition.",
     -39.68f, 19.00f, 4.05f, 1.95f, 1.50f, 1.0f, 0.0f},
    { 90, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_hivev02.tga",
     "Hive Fortress Revision II", "Boss Archive",
     "Pre-production architectural revision of the fortified Hive complex.",
     -39.68f, 22.60f, 1.90f, 1.95f, 1.50f, 1.0f, 0.0f},
    { 91, CeMuseumTour::kWingEarlyDesigns, "goodies\\ca_bosses\\ca_boss_kraken.tga",
     "Kraken Maritime Study", "Boss Archive",
     "Early large-scale naval war-machine silhouette and hull exploration.",
     -39.68f, 22.60f, 4.05f, 1.95f, 1.50f, 1.0f, 0.0f},
};

struct UploadSize {
    unsigned width;
    unsigned height;
};

// Bounded <= 256 px longest edge preserving exact source aspect ratio.
inline UploadSize BoundedUpload(unsigned srcW, unsigned srcH, unsigned maxEdge = kMaxTexEdge) {
    if (!srcW || !srcH || !maxEdge) return UploadSize{1u, 1u};
    const unsigned longest = srcW > srcH ? srcW : srcH;
    if (longest <= maxEdge) return UploadSize{srcW, srcH};
    const float scale = float(maxEdge) / float(longest);
    unsigned w = static_cast<unsigned>(float(srcW) * scale + 0.5f);
    unsigned h = static_cast<unsigned>(float(srcH) * scale + 0.5f);
    return UploadSize{w ? w : 1u, h ? h : 1u};
}

struct QuadExtent {
    float halfW;
    float halfH;
};

// Fit a source image inside [maxWidth x maxHeight] preserving exact aspect ratio.
inline QuadExtent FitFrame(unsigned srcW, unsigned srcH, float maxWidth, float maxHeight) {
    if (!srcW || !srcH || !(maxWidth > 0.0f) || !(maxHeight > 0.0f))
        return QuadExtent{maxWidth * 0.5f, maxHeight * 0.5f};
    const float aspect = float(srcW) / float(srcH);
    const float boxAspect = maxWidth / maxHeight;
    float w = maxWidth, h = maxHeight;
    if (aspect > boxAspect) h = maxWidth / aspect;
    else w = maxHeight * aspect;
    return QuadExtent{w * 0.5f, h * 0.5f};
}

// Wing-scoped gallery membership (used for plaque targeting & per-wing inventory counts).
inline bool ShouldBeResident(unsigned index, CeMuseumTour::Wing currentWing) {
    if (index >= kManifestCount) return false;
    return kManifest[index].wing == currentWing;
}

inline unsigned CountWantedResident(CeMuseumTour::Wing currentWing) {
    unsigned n = 0;
    for (unsigned i = 0; i < kManifestCount; ++i)
        if (ShouldBeResident(i, currentWing)) ++n;
    return n;
}

// Estimated worst-case RGBA memory bytes for resident wing canvases (or full 149-plate collection).
inline unsigned EstimateWingBytes(CeMuseumTour::Wing wing) {
    return CountWantedResident(wing) * kMaxTexEdge * kMaxTexEdge * 4u;
}

inline unsigned EstimateTotalBytes() {
    return kManifestCount * kMaxTexEdge * kMaxTexEdge * 4u;
}

// Pure prefix/truncated filename matcher for retail packed filenames that hit the original 64-char
// limit (e.g. goodie 95 '.t.aya' and goodie 103 '.tga(0.aya' which lack a full '(0)' key).
inline bool MatchesTruncatedCatalogPath(const char *catalogPath, const char *requestedPath) {
    if (!catalogPath || !requestedPath || !*catalogPath || !*requestedPath) return false;
    const char *base = catalogPath;
    for (const char *p = catalogPath; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    // Strip trailing ".aya" from catalog filename.
    unsigned baseLen = 0;
    while (base[baseLen]) ++baseLen;
    if (baseLen > 4u) {
        const char *tail = base + baseLen - 4u;
        auto lo = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c; };
        if (tail[0] == '.' && lo(tail[1]) == 'a' && lo(tail[2]) == 'y' && lo(tail[3]) == 'a')
            baseLen -= 4u;
    }
    if (baseLen < 24u) return false;
    // Compare normalized requested ('\\' or '/' -> '%') against base prefix/exact-before-'('.
    unsigned reqLen = 0;
    while (requestedPath[reqLen]) ++reqLen;
    if (reqLen < 24u) return false;
    auto norm = [](char c) -> char {
        if (c == '\\' || c == '/') return '%';
        return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c;
    };
    const unsigned cmpLen = baseLen < reqLen ? baseLen : reqLen;
    for (unsigned i = 0; i < cmpLen; ++i) {
        if (norm(base[i]) != norm(requestedPath[i])) return false;
    }
    // Accept either a truncated disk stem (baseLen < reqLen, e.g. ".t") OR truncated "(0" suffix after full match.
    if (baseLen <= reqLen) return baseLen >= 32u;
    return base[reqLen] == '(';
}

// Nearest framed canvas the visitor is standing near and looking toward (3D ray-aware for two-tier salon hang).
inline int FocusCanvas3D(float eyeX, float eyeY, float eyeZ,
                         float aimX, float aimY, float aimZ,
                         CeMuseumTour::Wing currentWing) {
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return -1;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    int best = -1;
    float bestScore = -1.0f;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, currentWing)) continue;
        const CanvasSpec &sp = kManifest[i];
        const float dx = sp.localX - eyeX;
        const float dy = sp.localY - eyeY;
        const float dz = sp.centreZ - eyeZ;
        const float d2D = std::sqrt(dx * dx + dy * dy);
        if (d2D < 0.3f || d2D > 7.5f) continue;
        // Visitor must be on the front (gallery) side of the painting.
        if ((-dx * sp.normalX - dy * sp.normalY) <= 0.1f) continue;
        const float d3D = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(d3D > 1e-4f)) continue;
        const float cosA = (dx * ux + dy * uy + dz * uz) / d3D;
        if (cosA < 0.85f) continue;
        const float score = cosA - 0.02f * d2D;
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// 2D convenience overload (aiming level at eye height 1.90m — matches lower tier centreZ).
inline int FocusCanvas(float visitorX, float visitorY, float forwardX, float forwardY,
                       CeMuseumTour::Wing currentWing) {
    return FocusCanvas3D(visitorX, visitorY, 1.90f, forwardX, forwardY, 0.0f, currentWing);
}

// Runtime adapter functions implemented in ce_museum_art.cpp.
void RuntimeInit();
void RuntimeShutdown();
void RuntimeUpdate(CeMuseumTour::Wing wing);
void RuntimeSetAim(float eyeX, float eyeY, float eyeZ, float aimX, float aimY, float aimZ);
bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap);
void RuntimeDraw(float floorZ, bool ownStateBlock = true);

} // namespace CeMuseumArt

#endif
