#include "engine/props_engine.h"

#include "core/objects.h"

#include <cstdio>

namespace rune {
    namespace {
        size_t optionalOffset(UClass* cls, const char* name)
        {
            UProperty* property = cls->findProperty(Name(name));
            return property ? property->offset : MissingProperty;
        }
    }

    actor::Offsets offsets_actor;
    pawn::Offsets offsets_pawn;
    playerpawn::Offsets offsets_playerpawn;
    levelinfo::Offsets offsets_levelinfo;
    zoneinfo::Offsets offsets_zoneinfo;
    skyzoneinfo::Offsets offsets_skyzoneinfo;
    warpzoneinfo::Offsets offsets_warpzoneinfo;
    gameinfo::Offsets offsets_gameinfo;
    brush::Offsets offsets_brush;
    mover::Offsets offsets_mover;
    polyobj::Offsets offsets_polyobj;
    navigationpoint::Offsets offsets_navigationpoint;
    inventory::Offsets offsets_inventory;
    weapon::Offsets offsets_weapon;
    projectile::Offsets offsets_projectile;
    decoration::Offsets offsets_decoration;
    canvas::Offsets offsets_canvas;
    console::Offsets offsets_console;
    hud::Offsets offsets_hud;
    bitmap::Offsets offsets_bitmap;
    texture::Offsets offsets_texture;
    player::Offsets offsets_player;
    light::Offsets offsets_light;
    info::Offsets offsets_info;
    playerreplicationinfo::Offsets offsets_playerreplicationinfo;
    decal::Offsets offsets_decal;
    teleporter::Offsets offsets_teleporter;
    scriptedtexture::Offsets offsets_scriptedtexture;
    carcass::Offsets offsets_carcass;
    pickup::Offsets offsets_pickup;
    pathnode::Offsets offsets_pathnode;
    trigger::Offsets offsets_trigger;
    effects::Offsets offsets_effects;
    spawnnotify::Offsets offsets_spawnnotify;
    keypoint::Offsets offsets_keypoint;
    mesh::Offsets offsets_mesh;
    particlesystem::Offsets offsets_particlesystem;

    void bindProps_engine()
    {
        {
            UClass* cls = objects::findClass("Engine.Actor");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Actor\n");
            } else {
                offsets_actor.bStatic = cls->findProperty(Name("bStatic"))->offset;
                offsets_actor.bHidden = cls->findProperty(Name("bHidden"))->offset;
                offsets_actor.bNoDelete = cls->findProperty(Name("bNoDelete"))->offset;
                offsets_actor.bReleaseLock = cls->findProperty(Name("bReleaseLock"))->offset;
                offsets_actor.bAnimFinished = cls->findProperty(Name("bAnimFinished"))->offset;
                offsets_actor.bAnimLoop = cls->findProperty(Name("bAnimLoop"))->offset;
                offsets_actor.bAnimNotify = cls->findProperty(Name("bAnimNotify"))->offset;
                offsets_actor.bAnimByOwner = cls->findProperty(Name("bAnimByOwner"))->offset;
                offsets_actor.bDeleteMe = cls->findProperty(Name("bDeleteMe"))->offset;
                offsets_actor.bAssimilated = cls->findProperty(Name("bAssimilated"))->offset;
                offsets_actor.bTicked = cls->findProperty(Name("bTicked"))->offset;
                offsets_actor.bLightChanged = cls->findProperty(Name("bLightChanged"))->offset;
                offsets_actor.bDynamicLight = cls->findProperty(Name("bDynamicLight"))->offset;
                offsets_actor.bTimerLoop = cls->findProperty(Name("bTimerLoop"))->offset;
                offsets_actor.bRenderedLastFrame = cls->findProperty(Name("bRenderedLastFrame"))->offset;
                offsets_actor.bSpecialRender = cls->findProperty(Name("bSpecialRender"))->offset;
                offsets_actor.bCanTeleport = cls->findProperty(Name("bCanTeleport"))->offset;
                offsets_actor.bOwnerNoSee = cls->findProperty(Name("bOwnerNoSee"))->offset;
                offsets_actor.bOnlyOwnerSee = cls->findProperty(Name("bOnlyOwnerSee"))->offset;
                offsets_actor.bIsMover = cls->findProperty(Name("bIsMover"))->offset;
                offsets_actor.bAlwaysRelevant = cls->findProperty(Name("bAlwaysRelevant"))->offset;
                offsets_actor.bAlwaysTick = cls->findProperty(Name("bAlwaysTick"))->offset;
                offsets_actor.bHighDetail = cls->findProperty(Name("bHighDetail"))->offset;
                offsets_actor.bStasis = cls->findProperty(Name("bStasis"))->offset;
                offsets_actor.bForceStasis = cls->findProperty(Name("bForceStasis"))->offset;
                offsets_actor.bIsPawn = cls->findProperty(Name("bIsPawn"))->offset;
                offsets_actor.bNetTemporary = cls->findProperty(Name("bNetTemporary"))->offset;
                offsets_actor.bNetOptional = cls->findProperty(Name("bNetOptional"))->offset;
                offsets_actor.bReplicateInstigator = cls->findProperty(Name("bReplicateInstigator"))->offset;
                offsets_actor.bTrailerSameRotation = cls->findProperty(Name("bTrailerSameRotation"))->offset;
                offsets_actor.bTrailerPrePivot = cls->findProperty(Name("bTrailerPrePivot"))->offset;
                offsets_actor.bClientAnim = cls->findProperty(Name("bClientAnim"))->offset;
                offsets_actor.bSimFall = cls->findProperty(Name("bSimFall"))->offset;
                offsets_actor.bFrameNotifies = cls->findProperty(Name("bFrameNotifies"))->offset;
                offsets_actor.bLookFocusPlayer = cls->findProperty(Name("bLookFocusPlayer"))->offset;
                offsets_actor.bLookFocusCreature = cls->findProperty(Name("bLookFocusCreature"))->offset;
                offsets_actor.bForceRender = cls->findProperty(Name("bForceRender"))->offset;
                offsets_actor.Physics = cls->findProperty(Name("Physics"))->offset;
                offsets_actor.Role = cls->findProperty(Name("Role"))->offset;
                offsets_actor.RemoteRole = cls->findProperty(Name("RemoteRole"))->offset;
                offsets_actor.NetTag = cls->findProperty(Name("NetTag"))->offset;
                offsets_actor.Owner = cls->findProperty(Name("Owner"))->offset;
                offsets_actor.InitialState = cls->findProperty(Name("InitialState"))->offset;
                offsets_actor.Group = cls->findProperty(Name("Group"))->offset;
                offsets_actor.TimerRate = cls->findProperty(Name("TimerRate"))->offset;
                offsets_actor.TimerCounter = cls->findProperty(Name("TimerCounter"))->offset;
                offsets_actor.LifeSpan = cls->findProperty(Name("LifeSpan"))->offset;
                offsets_actor.AnimSequence = cls->findProperty(Name("AnimSequence"))->offset;
                offsets_actor.AnimFrame = cls->findProperty(Name("AnimFrame"))->offset;
                offsets_actor.AnimRate = cls->findProperty(Name("AnimRate"))->offset;
                offsets_actor.TweenRate = cls->findProperty(Name("TweenRate"))->offset;
                offsets_actor.LODBias = cls->findProperty(Name("LODBias"))->offset;
                offsets_actor.Level = cls->findProperty(Name("Level"))->offset;
                offsets_actor.XLevel = cls->findProperty(Name("XLevel"))->offset;
                offsets_actor.Tag = cls->findProperty(Name("Tag"))->offset;
                offsets_actor.Event = cls->findProperty(Name("Event"))->offset;
                offsets_actor.Target = cls->findProperty(Name("Target"))->offset;
                offsets_actor.Instigator = cls->findProperty(Name("Instigator"))->offset;
                offsets_actor.Inventory = cls->findProperty(Name("Inventory"))->offset;
                offsets_actor.Base = cls->findProperty(Name("Base"))->offset;
                offsets_actor.BaseJoint = cls->findProperty(Name("BaseJoint"))->offset;
                offsets_actor.BaseMatterType = cls->findProperty(Name("BaseMatterType"))->offset;
                offsets_actor.BaseScrollDir = cls->findProperty(Name("BaseScrollDir"))->offset;
                offsets_actor.Region = cls->findProperty(Name("Region"))->offset;
                offsets_actor.AttachTag = cls->findProperty(Name("AttachTag"))->offset;
                offsets_actor.StandingCount = cls->findProperty(Name("StandingCount"))->offset;
                offsets_actor.MiscNumber = cls->findProperty(Name("MiscNumber"))->offset;
                offsets_actor.LatentByte = cls->findProperty(Name("LatentByte"))->offset;
                offsets_actor.LatentInt = cls->findProperty(Name("LatentInt"))->offset;
                offsets_actor.LatentFloat = cls->findProperty(Name("LatentFloat"))->offset;
                offsets_actor.LatentActor = cls->findProperty(Name("LatentActor"))->offset;
                offsets_actor.Touching = cls->findProperty(Name("Touching"))->offset;
                offsets_actor.Deleted = cls->findProperty(Name("Deleted"))->offset;
                offsets_actor.CollisionTag = cls->findProperty(Name("CollisionTag"))->offset;
                offsets_actor.LightingTag = cls->findProperty(Name("LightingTag"))->offset;
                offsets_actor.OtherTag = cls->findProperty(Name("OtherTag"))->offset;
                offsets_actor.ExtraTag = cls->findProperty(Name("ExtraTag"))->offset;
                offsets_actor.SpecialTag = cls->findProperty(Name("SpecialTag"))->offset;
                offsets_actor.Location = cls->findProperty(Name("Location"))->offset;
                offsets_actor.Rotation = cls->findProperty(Name("Rotation"))->offset;
                offsets_actor.OldLocation = cls->findProperty(Name("OldLocation"))->offset;
                offsets_actor.ColLocation = cls->findProperty(Name("ColLocation"))->offset;
                offsets_actor.Velocity = cls->findProperty(Name("Velocity"))->offset;
                offsets_actor.Acceleration = cls->findProperty(Name("Acceleration"))->offset;
                offsets_actor.OddsOfAppearing = cls->findProperty(Name("OddsOfAppearing"))->offset;
                offsets_actor.bHiddenEd = cls->findProperty(Name("bHiddenEd"))->offset;
                offsets_actor.bDirectional = cls->findProperty(Name("bDirectional"))->offset;
                offsets_actor.bSelected = cls->findProperty(Name("bSelected"))->offset;
                offsets_actor.bMemorized = cls->findProperty(Name("bMemorized"))->offset;
                offsets_actor.bHighlighted = cls->findProperty(Name("bHighlighted"))->offset;
                offsets_actor.bEdLocked = cls->findProperty(Name("bEdLocked"))->offset;
                offsets_actor.bEdShouldSnap = cls->findProperty(Name("bEdShouldSnap"))->offset;
                offsets_actor.bEdSnap = cls->findProperty(Name("bEdSnap"))->offset;
                offsets_actor.bTempEditor = cls->findProperty(Name("bTempEditor"))->offset;
                offsets_actor.bDifficulty0 = cls->findProperty(Name("bDifficulty0"))->offset;
                offsets_actor.bDifficulty1 = cls->findProperty(Name("bDifficulty1"))->offset;
                offsets_actor.bDifficulty2 = cls->findProperty(Name("bDifficulty2"))->offset;
                offsets_actor.bDifficulty3 = cls->findProperty(Name("bDifficulty3"))->offset;
                offsets_actor.bSinglePlayer = cls->findProperty(Name("bSinglePlayer"))->offset;
                offsets_actor.bNet = cls->findProperty(Name("bNet"))->offset;
                offsets_actor.bNetSpecial = cls->findProperty(Name("bNetSpecial"))->offset;
                offsets_actor.DrawType = cls->findProperty(Name("DrawType"))->offset;
                offsets_actor.Style = cls->findProperty(Name("Style"))->offset;
                offsets_actor.Sprite = cls->findProperty(Name("Sprite"))->offset;
                offsets_actor.Texture = cls->findProperty(Name("Texture"))->offset;
                offsets_actor.Skin = cls->findProperty(Name("Skin"))->offset;
                offsets_actor.Mesh = cls->findProperty(Name("Mesh"))->offset;
                offsets_actor.bHasShadow = cls->findProperty(Name("bHasShadow"))->offset;
                offsets_actor.ShadowTexture = cls->findProperty(Name("ShadowTexture"))->offset;
                offsets_actor.ShadowVector = cls->findProperty(Name("ShadowVector"))->offset;
                offsets_actor.Brush = cls->findProperty(Name("Brush"))->offset;
                offsets_actor.DrawScale = cls->findProperty(Name("DrawScale"))->offset;
                offsets_actor.PrePivot = cls->findProperty(Name("PrePivot"))->offset;
                offsets_actor.ScaleGlow = cls->findProperty(Name("ScaleGlow"))->offset;
                offsets_actor.VisibilityRadius = cls->findProperty(Name("VisibilityRadius"))->offset;
                offsets_actor.VisibilityHeight = cls->findProperty(Name("VisibilityHeight"))->offset;
                offsets_actor.AmbientGlow = cls->findProperty(Name("AmbientGlow"))->offset;
                offsets_actor.Fatness = cls->findProperty(Name("Fatness"))->offset;
                offsets_actor.SpriteProjForward = cls->findProperty(Name("SpriteProjForward"))->offset;
                offsets_actor.ColorAdjust = cls->findProperty(Name("ColorAdjust"))->offset;
                offsets_actor.DesiredColorAdjust = cls->findProperty(Name("DesiredColorAdjust"))->offset;
                offsets_actor.DesiredFatness = cls->findProperty(Name("DesiredFatness"))->offset;
                offsets_actor.AlphaScale = cls->findProperty(Name("AlphaScale"))->offset;
                offsets_actor.LODPolyCount = cls->findProperty(Name("LODPolyCount"))->offset;
                offsets_actor.LODDistMax = cls->findProperty(Name("LODDistMax"))->offset;
                offsets_actor.LODDistMin = cls->findProperty(Name("LODDistMin"))->offset;
                offsets_actor.LODPercentMin = cls->findProperty(Name("LODPercentMin"))->offset;
                offsets_actor.LODPercentMax = cls->findProperty(Name("LODPercentMax"))->offset;
                offsets_actor.LODCurve = cls->findProperty(Name("LODCurve"))->offset;
                offsets_actor.bUnlit = cls->findProperty(Name("bUnlit"))->offset;
                offsets_actor.bPointLight = cls->findProperty(Name("bPointLight"))->offset;
                offsets_actor.bMirrored = cls->findProperty(Name("bMirrored"))->offset;
                offsets_actor.bNoSmooth = cls->findProperty(Name("bNoSmooth"))->offset;
                offsets_actor.bParticles = cls->findProperty(Name("bParticles"))->offset;
                offsets_actor.bRandomFrame = cls->findProperty(Name("bRandomFrame"))->offset;
                offsets_actor.bMeshEnviroMap = cls->findProperty(Name("bMeshEnviroMap"))->offset;
                offsets_actor.bMeshCurvy = cls->findProperty(Name("bMeshCurvy"))->offset;
                offsets_actor.bFilterByVolume = cls->findProperty(Name("bFilterByVolume"))->offset;
                offsets_actor.bPreLight = cls->findProperty(Name("bPreLight"))->offset;
                offsets_actor.bComplexOcclusion = cls->findProperty(Name("bComplexOcclusion"))->offset;
                offsets_actor.bShadowCast = cls->findProperty(Name("bShadowCast"))->offset;
                offsets_actor.bGameRelevant = cls->findProperty(Name("bGameRelevant"))->offset;
                offsets_actor.bCarriedItem = cls->findProperty(Name("bCarriedItem"))->offset;
                offsets_actor.bForcePhysicsUpdate = cls->findProperty(Name("bForcePhysicsUpdate"))->offset;
                offsets_actor.bIsSecretGoal = cls->findProperty(Name("bIsSecretGoal"))->offset;
                offsets_actor.bIsKillGoal = cls->findProperty(Name("bIsKillGoal"))->offset;
                offsets_actor.bIsItemGoal = cls->findProperty(Name("bIsItemGoal"))->offset;
                offsets_actor.bCollideWhenPlacing = cls->findProperty(Name("bCollideWhenPlacing"))->offset;
                offsets_actor.bTravel = cls->findProperty(Name("bTravel"))->offset;
                offsets_actor.bMovable = cls->findProperty(Name("bMovable"))->offset;
                offsets_actor.AttachParent = cls->findProperty(Name("AttachParent"))->offset;
                offsets_actor.AttachParentJoint = cls->findProperty(Name("AttachParentJoint"))->offset;
                offsets_actor.MultiSkins = cls->findProperty(Name("MultiSkins"))->offset;
                offsets_actor.IncrementStat = optionalOffset(cls, "IncrementStat");
                offsets_actor.SoundRadius = cls->findProperty(Name("SoundRadius"))->offset;
                offsets_actor.SoundVolume = cls->findProperty(Name("SoundVolume"))->offset;
                offsets_actor.SoundPitch = cls->findProperty(Name("SoundPitch"))->offset;
                offsets_actor.AmbientSound = cls->findProperty(Name("AmbientSound"))->offset;
                offsets_actor.TransientSoundVolume = cls->findProperty(Name("TransientSoundVolume"))->offset;
                offsets_actor.TransientSoundRadius = cls->findProperty(Name("TransientSoundRadius"))->offset;
                offsets_actor.CollisionRadius = cls->findProperty(Name("CollisionRadius"))->offset;
                offsets_actor.CollisionHeight = cls->findProperty(Name("CollisionHeight"))->offset;
                offsets_actor.bCollideActors = cls->findProperty(Name("bCollideActors"))->offset;
                offsets_actor.bCollideWorld = cls->findProperty(Name("bCollideWorld"))->offset;
                offsets_actor.bBlockActors = cls->findProperty(Name("bBlockActors"))->offset;
                offsets_actor.bBlockPlayers = cls->findProperty(Name("bBlockPlayers"))->offset;
                offsets_actor.bProjTarget = cls->findProperty(Name("bProjTarget"))->offset;
                offsets_actor.bJointsBlock = cls->findProperty(Name("bJointsBlock"))->offset;
                offsets_actor.bJointsTouch = cls->findProperty(Name("bJointsTouch"))->offset;
                offsets_actor.bSweepable = cls->findProperty(Name("bSweepable"))->offset;
                offsets_actor.LightType = cls->findProperty(Name("LightType"))->offset;
                offsets_actor.LightEffect = cls->findProperty(Name("LightEffect"))->offset;
                offsets_actor.LightBrightness = cls->findProperty(Name("LightBrightness"))->offset;
                offsets_actor.LightHue = cls->findProperty(Name("LightHue"))->offset;
                offsets_actor.LightSaturation = cls->findProperty(Name("LightSaturation"))->offset;
                offsets_actor.LightRadius = cls->findProperty(Name("LightRadius"))->offset;
                offsets_actor.LightPeriod = cls->findProperty(Name("LightPeriod"))->offset;
                offsets_actor.LightPhase = cls->findProperty(Name("LightPhase"))->offset;
                offsets_actor.LightCone = cls->findProperty(Name("LightCone"))->offset;
                offsets_actor.VolumeBrightness = cls->findProperty(Name("VolumeBrightness"))->offset;
                offsets_actor.VolumeRadius = cls->findProperty(Name("VolumeRadius"))->offset;
                offsets_actor.VolumeFog = cls->findProperty(Name("VolumeFog"))->offset;
                offsets_actor.bSpecialLit = cls->findProperty(Name("bSpecialLit"))->offset;
                offsets_actor.bSpecialLit2 = cls->findProperty(Name("bSpecialLit2"))->offset;
                offsets_actor.bSpecialLit3 = cls->findProperty(Name("bSpecialLit3"))->offset;
                offsets_actor.bActorShadows = cls->findProperty(Name("bActorShadows"))->offset;
                offsets_actor.bCorona = cls->findProperty(Name("bCorona"))->offset;
                offsets_actor.bLensFlare = cls->findProperty(Name("bLensFlare"))->offset;
                offsets_actor.bNegativeLight = cls->findProperty(Name("bNegativeLight"))->offset;
                offsets_actor.bAffectWorld = cls->findProperty(Name("bAffectWorld"))->offset;
                offsets_actor.bAffectActors = cls->findProperty(Name("bAffectActors"))->offset;
                offsets_actor.bBounce = cls->findProperty(Name("bBounce"))->offset;
                offsets_actor.bFixedRotationDir = cls->findProperty(Name("bFixedRotationDir"))->offset;
                offsets_actor.bRotateToDesired = cls->findProperty(Name("bRotateToDesired"))->offset;
                offsets_actor.bInterpolating = cls->findProperty(Name("bInterpolating"))->offset;
                offsets_actor.bJustTeleported = cls->findProperty(Name("bJustTeleported"))->offset;
                offsets_actor.DodgeDir = cls->findProperty(Name("DodgeDir"))->offset;
                offsets_actor.Mass = cls->findProperty(Name("Mass"))->offset;
                offsets_actor.Buoyancy = cls->findProperty(Name("Buoyancy"))->offset;
                offsets_actor.RotationRate = cls->findProperty(Name("RotationRate"))->offset;
                offsets_actor.DesiredRotation = cls->findProperty(Name("DesiredRotation"))->offset;
                offsets_actor.PhysAlpha = cls->findProperty(Name("PhysAlpha"))->offset;
                offsets_actor.PhysRate = cls->findProperty(Name("PhysRate"))->offset;
                offsets_actor.PendingTouch = cls->findProperty(Name("PendingTouch"))->offset;
                offsets_actor.AnimLast = cls->findProperty(Name("AnimLast"))->offset;
                offsets_actor.AnimMinRate = cls->findProperty(Name("AnimMinRate"))->offset;
                offsets_actor.OldAnimRate = cls->findProperty(Name("OldAnimRate"))->offset;
                offsets_actor.SimAnim = cls->findProperty(Name("SimAnim"))->offset;
                offsets_actor.NetPriority = cls->findProperty(Name("NetPriority"))->offset;
                offsets_actor.NetUpdateFrequency = cls->findProperty(Name("NetUpdateFrequency"))->offset;
                offsets_actor.bNetInitial = cls->findProperty(Name("bNetInitial"))->offset;
                offsets_actor.bNetOwner = cls->findProperty(Name("bNetOwner"))->offset;
                offsets_actor.bNetRelevant = cls->findProperty(Name("bNetRelevant"))->offset;
                offsets_actor.bNetSee = cls->findProperty(Name("bNetSee"))->offset;
                offsets_actor.bNetHear = cls->findProperty(Name("bNetHear"))->offset;
                offsets_actor.bNetFeel = cls->findProperty(Name("bNetFeel"))->offset;
                offsets_actor.bSimulatedPawn = cls->findProperty(Name("bSimulatedPawn"))->offset;
                offsets_actor.bDemoRecording = cls->findProperty(Name("bDemoRecording"))->offset;
                offsets_actor.bClientDemoRecording = cls->findProperty(Name("bClientDemoRecording"))->offset;
                offsets_actor.bClientDemoNetFunc = cls->findProperty(Name("bClientDemoNetFunc"))->offset;
                offsets_actor.bNoSurfaceBob = cls->findProperty(Name("bNoSurfaceBob"))->offset;
                offsets_actor.bDrawSkel = cls->findProperty(Name("bDrawSkel"))->offset;
                offsets_actor.bDrawJoints = cls->findProperty(Name("bDrawJoints"))->offset;
                offsets_actor.bDrawAxes = cls->findProperty(Name("bDrawAxes"))->offset;
                offsets_actor.bApplyLagToAccelerators = cls->findProperty(Name("bApplyLagToAccelerators"))->offset;
                offsets_actor.SkelMesh = cls->findProperty(Name("SkelMesh"))->offset;
                offsets_actor.Skeletal = cls->findProperty(Name("Skeletal"))->offset;
                offsets_actor.SubstituteMesh = optionalOffset(cls, "SubstituteMesh");
                offsets_actor.BlendAnimAlpha = cls->findProperty(Name("BlendAnimAlpha"))->offset;
                offsets_actor.BlendAnimFrame = cls->findProperty(Name("BlendAnimFrame"))->offset;
                offsets_actor.BlendAnimSequence = cls->findProperty(Name("BlendAnimSequence"))->offset;
                offsets_actor.AnimProxy = cls->findProperty(Name("AnimProxy"))->offset;
                offsets_actor.SkelGroupSkins = cls->findProperty(Name("SkelGroupSkins"))->offset;
                offsets_actor.SkelGroupFlags = cls->findProperty(Name("SkelGroupFlags"))->offset;
                offsets_actor.JointFlags = cls->findProperty(Name("JointFlags"))->offset;
                offsets_actor.JointChild = cls->findProperty(Name("JointChild"))->offset;
                offsets_actor.RenderIteratorClass = cls->findProperty(Name("RenderIteratorClass"))->offset;
                offsets_actor.RenderInterface = cls->findProperty(Name("RenderInterface"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Pawn");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Pawn\n");
            } else {
                offsets_pawn.CarcassType = cls->findProperty(Name("CarcassType"))->offset;
                offsets_pawn.bBehindView = cls->findProperty(Name("bBehindView"))->offset;
                offsets_pawn.bIsPlayer = cls->findProperty(Name("bIsPlayer"))->offset;
                offsets_pawn.bJustLanded = cls->findProperty(Name("bJustLanded"))->offset;
                offsets_pawn.bUpAndOut = cls->findProperty(Name("bUpAndOut"))->offset;
                offsets_pawn.bIsWalking = cls->findProperty(Name("bIsWalking"))->offset;
                offsets_pawn.bHitSlopedWall = cls->findProperty(Name("bHitSlopedWall"))->offset;
                offsets_pawn.bNeverSwitchOnPickup = cls->findProperty(Name("bNeverSwitchOnPickup"))->offset;
                offsets_pawn.bWarping = cls->findProperty(Name("bWarping"))->offset;
                offsets_pawn.bUpdatingDisplay = cls->findProperty(Name("bUpdatingDisplay"))->offset;
                offsets_pawn.bCanStrafe = cls->findProperty(Name("bCanStrafe"))->offset;
                offsets_pawn.bFixedStart = cls->findProperty(Name("bFixedStart"))->offset;
                offsets_pawn.bReducedSpeed = cls->findProperty(Name("bReducedSpeed"))->offset;
                offsets_pawn.bCanJump = cls->findProperty(Name("bCanJump"))->offset;
                offsets_pawn.bCanWalk = cls->findProperty(Name("bCanWalk"))->offset;
                offsets_pawn.bCanSwim = cls->findProperty(Name("bCanSwim"))->offset;
                offsets_pawn.bCanFly = cls->findProperty(Name("bCanFly"))->offset;
                offsets_pawn.bCanOpenDoors = cls->findProperty(Name("bCanOpenDoors"))->offset;
                offsets_pawn.bCanDoSpecial = cls->findProperty(Name("bCanDoSpecial"))->offset;
                offsets_pawn.bDrowning = cls->findProperty(Name("bDrowning"))->offset;
                offsets_pawn.bLOSflag = cls->findProperty(Name("bLOSflag"))->offset;
                offsets_pawn.bFromWall = cls->findProperty(Name("bFromWall"))->offset;
                offsets_pawn.bHunting = cls->findProperty(Name("bHunting"))->offset;
                offsets_pawn.bAvoidLedges = cls->findProperty(Name("bAvoidLedges"))->offset;
                offsets_pawn.bStopAtLedges = cls->findProperty(Name("bStopAtLedges"))->offset;
                offsets_pawn.bJumpOffPawn = cls->findProperty(Name("bJumpOffPawn"))->offset;
                offsets_pawn.bShootSpecial = cls->findProperty(Name("bShootSpecial"))->offset;
                offsets_pawn.bAutoActivate = cls->findProperty(Name("bAutoActivate"))->offset;
                offsets_pawn.bIsHuman = cls->findProperty(Name("bIsHuman"))->offset;
                offsets_pawn.bIsFemale = cls->findProperty(Name("bIsFemale"))->offset;
                offsets_pawn.bIsMultiSkinned = cls->findProperty(Name("bIsMultiSkinned"))->offset;
                offsets_pawn.bCountJumps = cls->findProperty(Name("bCountJumps"))->offset;
                offsets_pawn.bAdvancedTactics = cls->findProperty(Name("bAdvancedTactics"))->offset;
                offsets_pawn.bViewTarget = cls->findProperty(Name("bViewTarget"))->offset;
                offsets_pawn.bCanGrabEdges = cls->findProperty(Name("bCanGrabEdges"))->offset;
                offsets_pawn.bAlignToFloor = cls->findProperty(Name("bAlignToFloor"))->offset;
                offsets_pawn.SightCounter = cls->findProperty(Name("SightCounter"))->offset;
                offsets_pawn.PainTime = cls->findProperty(Name("PainTime"))->offset;
                offsets_pawn.SpeechTime = cls->findProperty(Name("SpeechTime"))->offset;
                offsets_pawn.AmbientSoundTime = cls->findProperty(Name("AmbientSoundTime"))->offset;
                offsets_pawn.AvgPhysicsTime = cls->findProperty(Name("AvgPhysicsTime"))->offset;
                offsets_pawn.FootRegion = cls->findProperty(Name("FootRegion"))->offset;
                offsets_pawn.HeadRegion = cls->findProperty(Name("HeadRegion"))->offset;
                offsets_pawn.MoveTimer = cls->findProperty(Name("MoveTimer"))->offset;
                offsets_pawn.MoveTarget = cls->findProperty(Name("MoveTarget"))->offset;
                offsets_pawn.FaceTarget = cls->findProperty(Name("FaceTarget"))->offset;
                offsets_pawn.Destination = cls->findProperty(Name("Destination"))->offset;
                offsets_pawn.Focus = cls->findProperty(Name("Focus"))->offset;
                offsets_pawn.DesiredSpeed = cls->findProperty(Name("DesiredSpeed"))->offset;
                offsets_pawn.MaxDesiredSpeed = cls->findProperty(Name("MaxDesiredSpeed"))->offset;
                offsets_pawn.MeleeRange = cls->findProperty(Name("MeleeRange"))->offset;
                offsets_pawn.CombatRange = cls->findProperty(Name("CombatRange"))->offset;
                offsets_pawn.bStopMoveIfCombatRange = cls->findProperty(Name("bStopMoveIfCombatRange"))->offset;
                offsets_pawn.GroundSpeed = cls->findProperty(Name("GroundSpeed"))->offset;
                offsets_pawn.WaterSpeed = cls->findProperty(Name("WaterSpeed"))->offset;
                offsets_pawn.AirSpeed = cls->findProperty(Name("AirSpeed"))->offset;
                offsets_pawn.AccelRate = cls->findProperty(Name("AccelRate"))->offset;
                offsets_pawn.JumpZ = cls->findProperty(Name("JumpZ"))->offset;
                offsets_pawn.MaxStepHeight = cls->findProperty(Name("MaxStepHeight"))->offset;
                offsets_pawn.AirControl = cls->findProperty(Name("AirControl"))->offset;
                offsets_pawn.WalkingSpeed = cls->findProperty(Name("WalkingSpeed"))->offset;
                offsets_pawn.MovementSpeed = cls->findProperty(Name("MovementSpeed"))->offset;
                offsets_pawn.bHurrying = cls->findProperty(Name("bHurrying"))->offset;
                offsets_pawn.SpeedScale = cls->findProperty(Name("SpeedScale"))->offset;
                offsets_pawn.ClassID = cls->findProperty(Name("ClassID"))->offset;
                offsets_pawn.MinHitWall = cls->findProperty(Name("MinHitWall"))->offset;
                offsets_pawn.Visibility = cls->findProperty(Name("Visibility"))->offset;
                offsets_pawn.Alertness = cls->findProperty(Name("Alertness"))->offset;
                offsets_pawn.Stimulus = cls->findProperty(Name("Stimulus"))->offset;
                offsets_pawn.SightRadius = cls->findProperty(Name("SightRadius"))->offset;
                offsets_pawn.PeripheralVision = cls->findProperty(Name("PeripheralVision"))->offset;
                offsets_pawn.HearingThreshold = cls->findProperty(Name("HearingThreshold"))->offset;
                offsets_pawn.LastSeenPos = cls->findProperty(Name("LastSeenPos"))->offset;
                offsets_pawn.LastSeeingPos = cls->findProperty(Name("LastSeeingPos"))->offset;
                offsets_pawn.LastSeenTime = cls->findProperty(Name("LastSeenTime"))->offset;
                offsets_pawn.Enemy = cls->findProperty(Name("Enemy"))->offset;
                offsets_pawn.Weapon = cls->findProperty(Name("Weapon"))->offset;
                offsets_pawn.Shield = cls->findProperty(Name("Shield"))->offset;
                offsets_pawn.PendingWeapon = cls->findProperty(Name("PendingWeapon"))->offset;
                offsets_pawn.SelectedItem = cls->findProperty(Name("SelectedItem"))->offset;
                offsets_pawn.StartEvent = cls->findProperty(Name("StartEvent"))->offset;
                offsets_pawn.CurrentSkin = cls->findProperty(Name("CurrentSkin"))->offset;
                offsets_pawn.ViewRotation = cls->findProperty(Name("ViewRotation"))->offset;
                offsets_pawn.ViewLocation = cls->findProperty(Name("ViewLocation"))->offset;
                offsets_pawn.WalkBob = cls->findProperty(Name("WalkBob"))->offset;
                offsets_pawn.BaseEyeHeight = cls->findProperty(Name("BaseEyeHeight"))->offset;
                offsets_pawn.EyeHeight = cls->findProperty(Name("EyeHeight"))->offset;
                offsets_pawn.Floor = cls->findProperty(Name("Floor"))->offset;
                offsets_pawn.SplashTime = cls->findProperty(Name("SplashTime"))->offset;
                offsets_pawn.GrabLocationUp = cls->findProperty(Name("GrabLocationUp"))->offset;
                offsets_pawn.GrabLocationIn = cls->findProperty(Name("GrabLocationIn"))->offset;
                offsets_pawn.OrthoZoom = cls->findProperty(Name("OrthoZoom"))->offset;
                offsets_pawn.FovAngle = cls->findProperty(Name("FovAngle"))->offset;
                offsets_pawn.DieCount = cls->findProperty(Name("DieCount"))->offset;
                offsets_pawn.ItemCount = cls->findProperty(Name("ItemCount"))->offset;
                offsets_pawn.KillCount = cls->findProperty(Name("KillCount"))->offset;
                offsets_pawn.SecretCount = cls->findProperty(Name("SecretCount"))->offset;
                offsets_pawn.Spree = cls->findProperty(Name("Spree"))->offset;
                offsets_pawn.Health = cls->findProperty(Name("Health"))->offset;
                offsets_pawn.Strength = cls->findProperty(Name("Strength"))->offset;
                offsets_pawn.RunePower = cls->findProperty(Name("RunePower"))->offset;
                offsets_pawn.MaxHealth = cls->findProperty(Name("MaxHealth"))->offset;
                offsets_pawn.MaxStrength = cls->findProperty(Name("MaxStrength"))->offset;
                offsets_pawn.MaxPower = cls->findProperty(Name("MaxPower"))->offset;
                offsets_pawn.BodyPartHealth = cls->findProperty(Name("BodyPartHealth"))->offset;
                offsets_pawn.GibCount = cls->findProperty(Name("GibCount"))->offset;
                offsets_pawn.GibClass = cls->findProperty(Name("GibClass"))->offset;
                offsets_pawn.PainDelay = cls->findProperty(Name("PainDelay"))->offset;
                offsets_pawn.bGibbable = cls->findProperty(Name("bGibbable"))->offset;
                offsets_pawn.bInvisible = cls->findProperty(Name("bInvisible"))->offset;
                offsets_pawn.SelectionMesh = cls->findProperty(Name("SelectionMesh"))->offset;
                offsets_pawn.SpecialMesh = cls->findProperty(Name("SpecialMesh"))->offset;
                offsets_pawn.ReducedDamageType = cls->findProperty(Name("ReducedDamageType"))->offset;
                offsets_pawn.ReducedDamagePct = cls->findProperty(Name("ReducedDamagePct"))->offset;
                offsets_pawn.DropWhenKilled = cls->findProperty(Name("DropWhenKilled"))->offset;
                offsets_pawn.UnderWaterTime = cls->findProperty(Name("UnderWaterTime"))->offset;
                offsets_pawn.AttitudeToPlayer = cls->findProperty(Name("AttitudeToPlayer"))->offset;
                offsets_pawn.Intelligence = cls->findProperty(Name("Intelligence"))->offset;
                offsets_pawn.Skill = cls->findProperty(Name("Skill"))->offset;
                offsets_pawn.SpecialGoal = cls->findProperty(Name("SpecialGoal"))->offset;
                offsets_pawn.SpecialPause = cls->findProperty(Name("SpecialPause"))->offset;
                offsets_pawn.noise1spot = cls->findProperty(Name("noise1spot"))->offset;
                offsets_pawn.noise1time = cls->findProperty(Name("noise1time"))->offset;
                offsets_pawn.noise1other = cls->findProperty(Name("noise1other"))->offset;
                offsets_pawn.noise1loudness = cls->findProperty(Name("noise1loudness"))->offset;
                offsets_pawn.noise2spot = cls->findProperty(Name("noise2spot"))->offset;
                offsets_pawn.noise2time = cls->findProperty(Name("noise2time"))->offset;
                offsets_pawn.noise2other = cls->findProperty(Name("noise2other"))->offset;
                offsets_pawn.noise2loudness = cls->findProperty(Name("noise2loudness"))->offset;
                offsets_pawn.LastPainSound = cls->findProperty(Name("LastPainSound"))->offset;
                offsets_pawn.nextPawn = cls->findProperty(Name("nextPawn"))->offset;
                offsets_pawn.HitSound1 = cls->findProperty(Name("HitSound1"))->offset;
                offsets_pawn.HitSound2 = cls->findProperty(Name("HitSound2"))->offset;
                offsets_pawn.HitSound3 = cls->findProperty(Name("HitSound3"))->offset;
                offsets_pawn.Die = cls->findProperty(Name("Die"))->offset;
                offsets_pawn.Die2 = cls->findProperty(Name("Die2"))->offset;
                offsets_pawn.Die3 = cls->findProperty(Name("Die3"))->offset;
                offsets_pawn.WaterStep = cls->findProperty(Name("WaterStep"))->offset;
                offsets_pawn.GibSound = cls->findProperty(Name("GibSound"))->offset;
                offsets_pawn.FootstepVolume = cls->findProperty(Name("FootstepVolume"))->offset;
                offsets_pawn.LandGrunt = cls->findProperty(Name("LandGrunt"))->offset;
                offsets_pawn.FootStepWood = cls->findProperty(Name("FootStepWood"))->offset;
                offsets_pawn.FootStepMetal = cls->findProperty(Name("FootStepMetal"))->offset;
                offsets_pawn.FootStepStone = cls->findProperty(Name("FootStepStone"))->offset;
                offsets_pawn.FootStepFlesh = cls->findProperty(Name("FootStepFlesh"))->offset;
                offsets_pawn.FootStepIce = cls->findProperty(Name("FootStepIce"))->offset;
                offsets_pawn.FootStepEarth = cls->findProperty(Name("FootStepEarth"))->offset;
                offsets_pawn.FootStepSnow = cls->findProperty(Name("FootStepSnow"))->offset;
                offsets_pawn.FootStepBreakableWood = cls->findProperty(Name("FootStepBreakableWood"))->offset;
                offsets_pawn.FootStepBreakableStone = cls->findProperty(Name("FootStepBreakableStone"))->offset;
                offsets_pawn.FootStepWater = cls->findProperty(Name("FootStepWater"))->offset;
                offsets_pawn.FootStepMud = cls->findProperty(Name("FootStepMud"))->offset;
                offsets_pawn.FootStepLava = cls->findProperty(Name("FootStepLava"))->offset;
                offsets_pawn.LandSoundWood = cls->findProperty(Name("LandSoundWood"))->offset;
                offsets_pawn.LandSoundMetal = cls->findProperty(Name("LandSoundMetal"))->offset;
                offsets_pawn.LandSoundStone = cls->findProperty(Name("LandSoundStone"))->offset;
                offsets_pawn.LandSoundFlesh = cls->findProperty(Name("LandSoundFlesh"))->offset;
                offsets_pawn.LandSoundIce = cls->findProperty(Name("LandSoundIce"))->offset;
                offsets_pawn.LandSoundSnow = cls->findProperty(Name("LandSoundSnow"))->offset;
                offsets_pawn.LandSoundEarth = cls->findProperty(Name("LandSoundEarth"))->offset;
                offsets_pawn.LandSoundBreakableWood = cls->findProperty(Name("LandSoundBreakableWood"))->offset;
                offsets_pawn.LandSoundBreakableStone = cls->findProperty(Name("LandSoundBreakableStone"))->offset;
                offsets_pawn.LandSoundWater = cls->findProperty(Name("LandSoundWater"))->offset;
                offsets_pawn.LandSoundMud = cls->findProperty(Name("LandSoundMud"))->offset;
                offsets_pawn.LandSoundLava = cls->findProperty(Name("LandSoundLava"))->offset;
                offsets_pawn.bZoom = cls->findProperty(Name("bZoom"))->offset;
                offsets_pawn.bRun = cls->findProperty(Name("bRun"))->offset;
                offsets_pawn.bLook = cls->findProperty(Name("bLook"))->offset;
                offsets_pawn.bDuck = cls->findProperty(Name("bDuck"))->offset;
                offsets_pawn.bSnapLevel = cls->findProperty(Name("bSnapLevel"))->offset;
                offsets_pawn.bStrafe = cls->findProperty(Name("bStrafe"))->offset;
                offsets_pawn.bFire = cls->findProperty(Name("bFire"))->offset;
                offsets_pawn.bAltFire = cls->findProperty(Name("bAltFire"))->offset;
                offsets_pawn.bFreeLook = cls->findProperty(Name("bFreeLook"))->offset;
                offsets_pawn.bExtra0 = cls->findProperty(Name("bExtra0"))->offset;
                offsets_pawn.bExtra1 = cls->findProperty(Name("bExtra1"))->offset;
                offsets_pawn.bExtra2 = cls->findProperty(Name("bExtra2"))->offset;
                offsets_pawn.bExtra3 = cls->findProperty(Name("bExtra3"))->offset;
                offsets_pawn.CombatStyle = cls->findProperty(Name("CombatStyle"))->offset;
                offsets_pawn.NextState = cls->findProperty(Name("NextState"))->offset;
                offsets_pawn.NextLabel = cls->findProperty(Name("NextLabel"))->offset;
                offsets_pawn.NextStateAfterPain = cls->findProperty(Name("NextStateAfterPain"))->offset;
                offsets_pawn.SoundDampening = cls->findProperty(Name("SoundDampening"))->offset;
                offsets_pawn.DamageScaling = cls->findProperty(Name("DamageScaling"))->offset;
                offsets_pawn.PlayerReStartState = cls->findProperty(Name("PlayerReStartState"))->offset;
                offsets_pawn.MenuName = cls->findProperty(Name("MenuName"))->offset;
                offsets_pawn.NameArticle = cls->findProperty(Name("NameArticle"))->offset;
                offsets_pawn.VoicePitch = cls->findProperty(Name("VoicePitch"))->offset;
                offsets_pawn.VoiceType = cls->findProperty(Name("VoiceType"))->offset;
                offsets_pawn.OldMessageTime = cls->findProperty(Name("OldMessageTime"))->offset;
                offsets_pawn.WeaponJoint = cls->findProperty(Name("WeaponJoint"))->offset;
                offsets_pawn.ShieldJoint = cls->findProperty(Name("ShieldJoint"))->offset;
                offsets_pawn.StabJoint = cls->findProperty(Name("StabJoint"))->offset;
                offsets_pawn.RouteCache = cls->findProperty(Name("RouteCache"))->offset;
                offsets_pawn.PlayerReplicationInfoClass = cls->findProperty(Name("PlayerReplicationInfoClass"))->offset;
                offsets_pawn.PlayerReplicationInfo = cls->findProperty(Name("PlayerReplicationInfo"))->offset;
                offsets_pawn.shadow = cls->findProperty(Name("shadow"))->offset;
                offsets_pawn.bCanLook = cls->findProperty(Name("bCanLook"))->offset;
                offsets_pawn.MaxBodyAngle = cls->findProperty(Name("MaxBodyAngle"))->offset;
                offsets_pawn.MaxHeadAngle = cls->findProperty(Name("MaxHeadAngle"))->offset;
                offsets_pawn.bRotateHead = cls->findProperty(Name("bRotateHead"))->offset;
                offsets_pawn.bRotateTorso = cls->findProperty(Name("bRotateTorso"))->offset;
                offsets_pawn.bHeadLookUpDouble = cls->findProperty(Name("bHeadLookUpDouble"))->offset;
                offsets_pawn.LookDegPerSec = cls->findProperty(Name("LookDegPerSec"))->offset;
                offsets_pawn.LookTarget = cls->findProperty(Name("LookTarget"))->offset;
                offsets_pawn.LookSpot = cls->findProperty(Name("LookSpot"))->offset;
                offsets_pawn.LookAngle = cls->findProperty(Name("LookAngle"))->offset;
                offsets_pawn.targetangle = cls->findProperty(Name("targetangle"))->offset;
                offsets_pawn.bOverrideLookTarget = cls->findProperty(Name("bOverrideLookTarget"))->offset;
                offsets_pawn.MouthRot = cls->findProperty(Name("MouthRot"))->offset;
                offsets_pawn.DesiredMouthRot = cls->findProperty(Name("DesiredMouthRot"))->offset;
                offsets_pawn.MouthRotRate = cls->findProperty(Name("MouthRotRate"))->offset;
                offsets_pawn.MaxMouthRot = cls->findProperty(Name("MaxMouthRot"))->offset;
                offsets_pawn.MaxMouthRotRate = cls->findProperty(Name("MaxMouthRotRate"))->offset;
                offsets_pawn.FootprintClass = cls->findProperty(Name("FootprintClass"))->offset;
                offsets_pawn.WetFootprintClass = cls->findProperty(Name("WetFootprintClass"))->offset;
                offsets_pawn.BloodyFootprintClass = cls->findProperty(Name("BloodyFootprintClass"))->offset;
                offsets_pawn.LFootJoint = cls->findProperty(Name("LFootJoint"))->offset;
                offsets_pawn.RFootJoint = cls->findProperty(Name("RFootJoint"))->offset;
                offsets_pawn.bFootsteps = cls->findProperty(Name("bFootsteps"))->offset;
                offsets_pawn.WaterSteps = cls->findProperty(Name("WaterSteps"))->offset;
                offsets_pawn.BloodSteps = cls->findProperty(Name("BloodSteps"))->offset;
                offsets_pawn.bSwingingHigh = cls->findProperty(Name("bSwingingHigh"))->offset;
                offsets_pawn.bSwingingLow = cls->findProperty(Name("bSwingingLow"))->offset;
                offsets_pawn.DeathRadius = cls->findProperty(Name("DeathRadius"))->offset;
                offsets_pawn.DeathHeight = cls->findProperty(Name("DeathHeight"))->offset;
                offsets_pawn.bAllowStandOn = cls->findProperty(Name("bAllowStandOn"))->offset;
                offsets_pawn.bLeadEnemy = cls->findProperty(Name("bLeadEnemy"))->offset;
                offsets_pawn.UninterruptedAnim = cls->findProperty(Name("UninterruptedAnim"))->offset;
                offsets_pawn.UseActor = cls->findProperty(Name("UseActor"))->offset;
                offsets_pawn.SkinDefaultText = cls->findProperty(Name("SkinDefaultText"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.PlayerPawn");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.PlayerPawn\n");
            } else {
                offsets_playerpawn.Player = cls->findProperty(Name("Player"))->offset;
                offsets_playerpawn.Password = cls->findProperty(Name("Password"))->offset;
                offsets_playerpawn.DodgeClickTimer = cls->findProperty(Name("DodgeClickTimer"))->offset;
                offsets_playerpawn.DodgeClickTime = cls->findProperty(Name("DodgeClickTime"))->offset;
                offsets_playerpawn.Bob = cls->findProperty(Name("Bob"))->offset;
                offsets_playerpawn.LandBob = cls->findProperty(Name("LandBob"))->offset;
                offsets_playerpawn.AppliedBob = cls->findProperty(Name("AppliedBob"))->offset;
                offsets_playerpawn.bobtime = cls->findProperty(Name("bobtime"))->offset;
                offsets_playerpawn.ShowFlags = cls->findProperty(Name("ShowFlags"))->offset;
                offsets_playerpawn.RendMap = cls->findProperty(Name("RendMap"))->offset;
                offsets_playerpawn.Misc1 = cls->findProperty(Name("Misc1"))->offset;
                offsets_playerpawn.Misc2 = cls->findProperty(Name("Misc2"))->offset;
                offsets_playerpawn.ViewTarget = cls->findProperty(Name("ViewTarget"))->offset;
                offsets_playerpawn.FlashScale = cls->findProperty(Name("FlashScale"))->offset;
                offsets_playerpawn.FlashFog = cls->findProperty(Name("FlashFog"))->offset;
                offsets_playerpawn.myHUD = cls->findProperty(Name("myHUD"))->offset;
                offsets_playerpawn.myDebugHUD = cls->findProperty(Name("myDebugHUD"))->offset;
                offsets_playerpawn.Scoring = cls->findProperty(Name("Scoring"))->offset;
                offsets_playerpawn.HUDType = cls->findProperty(Name("HUDType"))->offset;
                offsets_playerpawn.ScoringType = cls->findProperty(Name("ScoringType"))->offset;
                offsets_playerpawn.DesiredFlashScale = cls->findProperty(Name("DesiredFlashScale"))->offset;
                offsets_playerpawn.ConstantGlowScale = cls->findProperty(Name("ConstantGlowScale"))->offset;
                offsets_playerpawn.InstantFlash = cls->findProperty(Name("InstantFlash"))->offset;
                offsets_playerpawn.DesiredFlashFog = cls->findProperty(Name("DesiredFlashFog"))->offset;
                offsets_playerpawn.ConstantGlowFog = cls->findProperty(Name("ConstantGlowFog"))->offset;
                offsets_playerpawn.InstantFog = cls->findProperty(Name("InstantFog"))->offset;
                offsets_playerpawn.DesiredFOV = cls->findProperty(Name("DesiredFOV"))->offset;
                offsets_playerpawn.DefaultFOV = cls->findProperty(Name("DefaultFOV"))->offset;
                offsets_playerpawn.HudTranslucency = cls->findProperty(Name("HudTranslucency"))->offset;
                offsets_playerpawn.Song = cls->findProperty(Name("Song"))->offset;
                offsets_playerpawn.SongSection = cls->findProperty(Name("SongSection"))->offset;
                offsets_playerpawn.CdTrack = cls->findProperty(Name("CdTrack"))->offset;
                offsets_playerpawn.Transition = cls->findProperty(Name("Transition"))->offset;
                offsets_playerpawn.shaketimer = cls->findProperty(Name("shaketimer"))->offset;
                offsets_playerpawn.shakemag = cls->findProperty(Name("shakemag"))->offset;
                offsets_playerpawn.shakevert = cls->findProperty(Name("shakevert"))->offset;
                offsets_playerpawn.maxshake = cls->findProperty(Name("maxshake"))->offset;
                offsets_playerpawn.verttimer = cls->findProperty(Name("verttimer"))->offset;
                offsets_playerpawn.MyAutoAim = cls->findProperty(Name("MyAutoAim"))->offset;
                offsets_playerpawn.JumpSound = cls->findProperty(Name("JumpSound"))->offset;
                offsets_playerpawn.AtrophyTimer = cls->findProperty(Name("AtrophyTimer"))->offset;
                offsets_playerpawn.bBloodLust = cls->findProperty(Name("bBloodLust"))->offset;
                offsets_playerpawn.PolyColorAdjust = cls->findProperty(Name("PolyColorAdjust"))->offset;
                offsets_playerpawn.DesiredPolyColorAdjust = cls->findProperty(Name("DesiredPolyColorAdjust"))->offset;
                offsets_playerpawn.bClientSideAlpha = cls->findProperty(Name("bClientSideAlpha"))->offset;
                offsets_playerpawn.ClientSideAlphaScale = cls->findProperty(Name("ClientSideAlphaScale"))->offset;
                offsets_playerpawn.OldStyle = cls->findProperty(Name("OldStyle"))->offset;
                offsets_playerpawn.OldScale = cls->findProperty(Name("OldScale"))->offset;
                offsets_playerpawn.StowSpot = cls->findProperty(Name("StowSpot"))->offset;
                offsets_playerpawn.bAdmin = cls->findProperty(Name("bAdmin"))->offset;
                offsets_playerpawn.bLookUpStairs = cls->findProperty(Name("bLookUpStairs"))->offset;
                offsets_playerpawn.bSnapToLevel = cls->findProperty(Name("bSnapToLevel"))->offset;
                offsets_playerpawn.bAlwaysMouseLook = cls->findProperty(Name("bAlwaysMouseLook"))->offset;
                offsets_playerpawn.bKeyboardLook = cls->findProperty(Name("bKeyboardLook"))->offset;
                offsets_playerpawn.bWasForward = cls->findProperty(Name("bWasForward"))->offset;
                offsets_playerpawn.bWasBack = cls->findProperty(Name("bWasBack"))->offset;
                offsets_playerpawn.bWasLeft = cls->findProperty(Name("bWasLeft"))->offset;
                offsets_playerpawn.bWasRight = cls->findProperty(Name("bWasRight"))->offset;
                offsets_playerpawn.bEdgeForward = cls->findProperty(Name("bEdgeForward"))->offset;
                offsets_playerpawn.bEdgeBack = cls->findProperty(Name("bEdgeBack"))->offset;
                offsets_playerpawn.bEdgeLeft = cls->findProperty(Name("bEdgeLeft"))->offset;
                offsets_playerpawn.bEdgeRight = cls->findProperty(Name("bEdgeRight"))->offset;
                offsets_playerpawn.bIsCrouching = cls->findProperty(Name("bIsCrouching"))->offset;
                offsets_playerpawn.bShakeDir = cls->findProperty(Name("bShakeDir"))->offset;
                offsets_playerpawn.bAnimTransition = cls->findProperty(Name("bAnimTransition"))->offset;
                offsets_playerpawn.bIsTurning = cls->findProperty(Name("bIsTurning"))->offset;
                offsets_playerpawn.bFrozen = cls->findProperty(Name("bFrozen"))->offset;
                offsets_playerpawn.bBadConnectionAlert = cls->findProperty(Name("bBadConnectionAlert"))->offset;
                offsets_playerpawn.bInvertMouse = cls->findProperty(Name("bInvertMouse"))->offset;
                offsets_playerpawn.bShowScores = cls->findProperty(Name("bShowScores"))->offset;
                offsets_playerpawn.bShowMenu = cls->findProperty(Name("bShowMenu"))->offset;
                offsets_playerpawn.bSpecialMenu = cls->findProperty(Name("bSpecialMenu"))->offset;
                offsets_playerpawn.bWokeUp = cls->findProperty(Name("bWokeUp"))->offset;
                offsets_playerpawn.bPressedJump = cls->findProperty(Name("bPressedJump"))->offset;
                offsets_playerpawn.bUpdatePosition = cls->findProperty(Name("bUpdatePosition"))->offset;
                offsets_playerpawn.bDelayedCommand = cls->findProperty(Name("bDelayedCommand"))->offset;
                offsets_playerpawn.bRising = cls->findProperty(Name("bRising"))->offset;
                offsets_playerpawn.bReducedVis = cls->findProperty(Name("bReducedVis"))->offset;
                offsets_playerpawn.bCenterView = cls->findProperty(Name("bCenterView"))->offset;
                offsets_playerpawn.bMaxMouseSmoothing = cls->findProperty(Name("bMaxMouseSmoothing"))->offset;
                offsets_playerpawn.bMouseZeroed = cls->findProperty(Name("bMouseZeroed"))->offset;
                offsets_playerpawn.bReadyToPlay = cls->findProperty(Name("bReadyToPlay"))->offset;
                offsets_playerpawn.bNoFlash = cls->findProperty(Name("bNoFlash"))->offset;
                offsets_playerpawn.bNoVoices = cls->findProperty(Name("bNoVoices"))->offset;
                offsets_playerpawn.bMessageBeep = cls->findProperty(Name("bMessageBeep"))->offset;
                offsets_playerpawn.bZooming = cls->findProperty(Name("bZooming"))->offset;
                offsets_playerpawn.bSinglePlayer = cls->findProperty(Name("bSinglePlayer"))->offset;
                offsets_playerpawn.bJustFired = cls->findProperty(Name("bJustFired"))->offset;
                offsets_playerpawn.bJustAltFired = cls->findProperty(Name("bJustAltFired"))->offset;
                offsets_playerpawn.bIsTyping = cls->findProperty(Name("bIsTyping"))->offset;
                offsets_playerpawn.bFixedCamera = cls->findProperty(Name("bFixedCamera"))->offset;
                offsets_playerpawn.bNeverAutoSwitch = cls->findProperty(Name("bNeverAutoSwitch"))->offset;
                offsets_playerpawn.bJumpStatus = cls->findProperty(Name("bJumpStatus"))->offset;
                offsets_playerpawn.bUpdating = cls->findProperty(Name("bUpdating"))->offset;
                offsets_playerpawn.bCheatsEnabled = cls->findProperty(Name("bCheatsEnabled"))->offset;
                offsets_playerpawn.bJustSpawned = cls->findProperty(Name("bJustSpawned"))->offset;
                offsets_playerpawn.ZoomLevel = cls->findProperty(Name("ZoomLevel"))->offset;
                offsets_playerpawn.LevelFadeAlpha = cls->findProperty(Name("LevelFadeAlpha"))->offset;
                offsets_playerpawn.SpecialMenu = cls->findProperty(Name("SpecialMenu"))->offset;
                offsets_playerpawn.DelayedCommand = cls->findProperty(Name("DelayedCommand"))->offset;
                offsets_playerpawn.MouseSensitivity = cls->findProperty(Name("MouseSensitivity"))->offset;
                offsets_playerpawn.WeaponPriority = cls->findProperty(Name("WeaponPriority"))->offset;
                offsets_playerpawn.SmoothMouseX = cls->findProperty(Name("SmoothMouseX"))->offset;
                offsets_playerpawn.SmoothMouseY = cls->findProperty(Name("SmoothMouseY"))->offset;
                offsets_playerpawn.BorrowedMouseX = cls->findProperty(Name("BorrowedMouseX"))->offset;
                offsets_playerpawn.BorrowedMouseY = cls->findProperty(Name("BorrowedMouseY"))->offset;
                offsets_playerpawn.MouseSmoothThreshold = cls->findProperty(Name("MouseSmoothThreshold"))->offset;
                offsets_playerpawn.MouseZeroTime = cls->findProperty(Name("MouseZeroTime"))->offset;
                offsets_playerpawn.aBaseX = cls->findProperty(Name("aBaseX"))->offset;
                offsets_playerpawn.aBaseY = cls->findProperty(Name("aBaseY"))->offset;
                offsets_playerpawn.aBaseZ = cls->findProperty(Name("aBaseZ"))->offset;
                offsets_playerpawn.aMouseX = cls->findProperty(Name("aMouseX"))->offset;
                offsets_playerpawn.aMouseY = cls->findProperty(Name("aMouseY"))->offset;
                offsets_playerpawn.aForward = cls->findProperty(Name("aForward"))->offset;
                offsets_playerpawn.aTurn = cls->findProperty(Name("aTurn"))->offset;
                offsets_playerpawn.aStrafe = cls->findProperty(Name("aStrafe"))->offset;
                offsets_playerpawn.aUp = cls->findProperty(Name("aUp"))->offset;
                offsets_playerpawn.aLookUp = cls->findProperty(Name("aLookUp"))->offset;
                offsets_playerpawn.aExtra4 = cls->findProperty(Name("aExtra4"))->offset;
                offsets_playerpawn.aExtra3 = cls->findProperty(Name("aExtra3"))->offset;
                offsets_playerpawn.aExtra2 = cls->findProperty(Name("aExtra2"))->offset;
                offsets_playerpawn.aExtra1 = cls->findProperty(Name("aExtra1"))->offset;
                offsets_playerpawn.aExtra0 = cls->findProperty(Name("aExtra0"))->offset;
                offsets_playerpawn.SavedMoves = cls->findProperty(Name("SavedMoves"))->offset;
                offsets_playerpawn.FreeMoves = cls->findProperty(Name("FreeMoves"))->offset;
                offsets_playerpawn.PendingMove = cls->findProperty(Name("PendingMove"))->offset;
                offsets_playerpawn.CurrentTimeStamp = cls->findProperty(Name("CurrentTimeStamp"))->offset;
                offsets_playerpawn.LastUpdateTime = cls->findProperty(Name("LastUpdateTime"))->offset;
                offsets_playerpawn.ServerTimeStamp = cls->findProperty(Name("ServerTimeStamp"))->offset;
                offsets_playerpawn.TimeMargin = cls->findProperty(Name("TimeMargin"))->offset;
                offsets_playerpawn.ClientUpdateTime = cls->findProperty(Name("ClientUpdateTime"))->offset;
                offsets_playerpawn.MaxTimeMargin = cls->findProperty(Name("MaxTimeMargin"))->offset;
                offsets_playerpawn.ProgressMessage = cls->findProperty(Name("ProgressMessage"))->offset;
                offsets_playerpawn.ProgressColor = cls->findProperty(Name("ProgressColor"))->offset;
                offsets_playerpawn.ProgressTimeOut = cls->findProperty(Name("ProgressTimeOut"))->offset;
                offsets_playerpawn.QuickSaveString = cls->findProperty(Name("QuickSaveString"))->offset;
                offsets_playerpawn.NoPauseMessage = cls->findProperty(Name("NoPauseMessage"))->offset;
                offsets_playerpawn.ViewingFrom = cls->findProperty(Name("ViewingFrom"))->offset;
                offsets_playerpawn.OwnCamera = cls->findProperty(Name("OwnCamera"))->offset;
                offsets_playerpawn.FailedView = cls->findProperty(Name("FailedView"))->offset;
                offsets_playerpawn.GameReplicationInfo = cls->findProperty(Name("GameReplicationInfo"))->offset;
                offsets_playerpawn.ngWorldSecret = cls->findProperty(Name("ngWorldSecret"))->offset;
                offsets_playerpawn.TargetViewRotation = cls->findProperty(Name("TargetViewRotation"))->offset;
                offsets_playerpawn.TargetEyeHeight = cls->findProperty(Name("TargetEyeHeight"))->offset;
                offsets_playerpawn.TargetWeaponViewOffset = cls->findProperty(Name("TargetWeaponViewOffset"))->offset;
                offsets_playerpawn.DemoViewPitch = cls->findProperty(Name("DemoViewPitch"))->offset;
                offsets_playerpawn.DemoViewYaw = cls->findProperty(Name("DemoViewYaw"))->offset;
                offsets_playerpawn.LastPlaySound = cls->findProperty(Name("LastPlaySound"))->offset;
                offsets_playerpawn.oldPhysics = optionalOffset(cls, "oldPhysics");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.LevelInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.LevelInfo\n");
            } else {
                offsets_levelinfo.TimeDilation = cls->findProperty(Name("TimeDilation"))->offset;
                offsets_levelinfo.TimeSeconds = cls->findProperty(Name("TimeSeconds"))->offset;
                offsets_levelinfo.Year = cls->findProperty(Name("Year"))->offset;
                offsets_levelinfo.Month = cls->findProperty(Name("Month"))->offset;
                offsets_levelinfo.Day = cls->findProperty(Name("Day"))->offset;
                offsets_levelinfo.DayOfWeek = cls->findProperty(Name("DayOfWeek"))->offset;
                offsets_levelinfo.Hour = cls->findProperty(Name("Hour"))->offset;
                offsets_levelinfo.Minute = cls->findProperty(Name("Minute"))->offset;
                offsets_levelinfo.Second = cls->findProperty(Name("Second"))->offset;
                offsets_levelinfo.Millisecond = cls->findProperty(Name("Millisecond"))->offset;
                offsets_levelinfo.Title = cls->findProperty(Name("Title"))->offset;
                offsets_levelinfo.Author = cls->findProperty(Name("Author"))->offset;
                offsets_levelinfo.IdealPlayerCount = cls->findProperty(Name("IdealPlayerCount"))->offset;
                offsets_levelinfo.RecommendedEnemies = cls->findProperty(Name("RecommendedEnemies"))->offset;
                offsets_levelinfo.RecommendedTeammates = cls->findProperty(Name("RecommendedTeammates"))->offset;
                offsets_levelinfo.LevelEnterText = cls->findProperty(Name("LevelEnterText"))->offset;
                offsets_levelinfo.LocalizedPkg = cls->findProperty(Name("LocalizedPkg"))->offset;
                offsets_levelinfo.Pauser = cls->findProperty(Name("Pauser"))->offset;
                offsets_levelinfo.Summary = cls->findProperty(Name("Summary"))->offset;
                offsets_levelinfo.bLonePlayer = cls->findProperty(Name("bLonePlayer"))->offset;
                offsets_levelinfo.bBegunPlay = cls->findProperty(Name("bBegunPlay"))->offset;
                offsets_levelinfo.bPlayersOnly = cls->findProperty(Name("bPlayersOnly"))->offset;
                offsets_levelinfo.bHighDetailMode = cls->findProperty(Name("bHighDetailMode"))->offset;
                offsets_levelinfo.bDropDetail = cls->findProperty(Name("bDropDetail"))->offset;
                offsets_levelinfo.bAggressiveLOD = cls->findProperty(Name("bAggressiveLOD"))->offset;
                offsets_levelinfo.bStartup = cls->findProperty(Name("bStartup"))->offset;
                offsets_levelinfo.bHumansOnly = cls->findProperty(Name("bHumansOnly"))->offset;
                offsets_levelinfo.bNoCheating = cls->findProperty(Name("bNoCheating"))->offset;
                offsets_levelinfo.bAllowFOV = cls->findProperty(Name("bAllowFOV"))->offset;
                offsets_levelinfo.bIntro = optionalOffset(cls, "bIntro");
                offsets_levelinfo.bFadeIn = cls->findProperty(Name("bFadeIn"))->offset;
                offsets_levelinfo.FadeRate = cls->findProperty(Name("FadeRate"))->offset;
                offsets_levelinfo.Song = cls->findProperty(Name("Song"))->offset;
                offsets_levelinfo.SongSection = cls->findProperty(Name("SongSection"))->offset;
                offsets_levelinfo.CdTrack = cls->findProperty(Name("CdTrack"))->offset;
                offsets_levelinfo.CdTrackLength = optionalOffset(cls, "CdTrackLength");
                offsets_levelinfo.PlayerDoppler = cls->findProperty(Name("PlayerDoppler"))->offset;
                offsets_levelinfo.Brightness = cls->findProperty(Name("Brightness"))->offset;
                offsets_levelinfo.Screenshot = cls->findProperty(Name("Screenshot"))->offset;
                offsets_levelinfo.DefaultTexture = cls->findProperty(Name("DefaultTexture"))->offset;
                offsets_levelinfo.HubStackLevel = cls->findProperty(Name("HubStackLevel"))->offset;
                offsets_levelinfo.LevelAction = cls->findProperty(Name("LevelAction"))->offset;
                offsets_levelinfo.bNeverPrecache = cls->findProperty(Name("bNeverPrecache"))->offset;
                offsets_levelinfo.NetMode = cls->findProperty(Name("NetMode"))->offset;
                offsets_levelinfo.ComputerName = cls->findProperty(Name("ComputerName"))->offset;
                offsets_levelinfo.EngineVersion = cls->findProperty(Name("EngineVersion"))->offset;
                offsets_levelinfo.MinNetVersion = cls->findProperty(Name("MinNetVersion"))->offset;
                offsets_levelinfo.GameVersion = cls->findProperty(Name("GameVersion"))->offset;
                offsets_levelinfo.BuildNumber = cls->findProperty(Name("BuildNumber"))->offset;
                offsets_levelinfo.DefaultGameType = cls->findProperty(Name("DefaultGameType"))->offset;
                offsets_levelinfo.Game = cls->findProperty(Name("Game"))->offset;
                offsets_levelinfo.NavigationPointList = cls->findProperty(Name("NavigationPointList"))->offset;
                offsets_levelinfo.PawnList = cls->findProperty(Name("PawnList"))->offset;
                offsets_levelinfo.NextURL = cls->findProperty(Name("NextURL"))->offset;
                offsets_levelinfo.bNextItems = cls->findProperty(Name("bNextItems"))->offset;
                offsets_levelinfo.NextSwitchCountdown = cls->findProperty(Name("NextSwitchCountdown"))->offset;
                offsets_levelinfo.AIProfile = cls->findProperty(Name("AIProfile"))->offset;
                offsets_levelinfo.AvgAITime = cls->findProperty(Name("AvgAITime"))->offset;
                offsets_levelinfo.bCheckWalkSurfaces = cls->findProperty(Name("bCheckWalkSurfaces"))->offset;
                offsets_levelinfo.SpawnNotify = cls->findProperty(Name("SpawnNotify"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.ZoneInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.ZoneInfo\n");
            } else {
                offsets_zoneinfo.ZoneTag = cls->findProperty(Name("ZoneTag"))->offset;
                offsets_zoneinfo.ZoneGravity = cls->findProperty(Name("ZoneGravity"))->offset;
                offsets_zoneinfo.ZoneVelocity = cls->findProperty(Name("ZoneVelocity"))->offset;
                offsets_zoneinfo.ZoneGroundFriction = cls->findProperty(Name("ZoneGroundFriction"))->offset;
                offsets_zoneinfo.ZoneFluidFriction = cls->findProperty(Name("ZoneFluidFriction"))->offset;
                offsets_zoneinfo.ZoneTerminalVelocity = cls->findProperty(Name("ZoneTerminalVelocity"))->offset;
                offsets_zoneinfo.ZonePlayerEvent = cls->findProperty(Name("ZonePlayerEvent"))->offset;
                offsets_zoneinfo.ZonePlayerExitEvent = cls->findProperty(Name("ZonePlayerExitEvent"))->offset;
                offsets_zoneinfo.ZonePlayerDiedEvent = cls->findProperty(Name("ZonePlayerDiedEvent"))->offset;
                offsets_zoneinfo.ZonePlayerCount = cls->findProperty(Name("ZonePlayerCount"))->offset;
                offsets_zoneinfo.NumCarcasses = cls->findProperty(Name("NumCarcasses"))->offset;
                offsets_zoneinfo.DamagePerSec = cls->findProperty(Name("DamagePerSec"))->offset;
                offsets_zoneinfo.DamageType = cls->findProperty(Name("DamageType"))->offset;
                offsets_zoneinfo.DamageString = cls->findProperty(Name("DamageString"))->offset;
                offsets_zoneinfo.ZoneName = cls->findProperty(Name("ZoneName"))->offset;
                offsets_zoneinfo.locationid = cls->findProperty(Name("locationid"))->offset;
                offsets_zoneinfo.MaxCarcasses = cls->findProperty(Name("MaxCarcasses"))->offset;
                offsets_zoneinfo.EntrySound = cls->findProperty(Name("EntrySound"))->offset;
                offsets_zoneinfo.EntrySoundBig = cls->findProperty(Name("EntrySoundBig"))->offset;
                offsets_zoneinfo.ExitSound = cls->findProperty(Name("ExitSound"))->offset;
                offsets_zoneinfo.EntryActor = cls->findProperty(Name("EntryActor"))->offset;
                offsets_zoneinfo.ExitActor = cls->findProperty(Name("ExitActor"))->offset;
                offsets_zoneinfo.bTakeOverCamera = cls->findProperty(Name("bTakeOverCamera"))->offset;
                offsets_zoneinfo.MaxCameraDist = cls->findProperty(Name("MaxCameraDist"))->offset;
                offsets_zoneinfo.SkyZoneName = cls->findProperty(Name("SkyZoneName"))->offset;
                offsets_zoneinfo.SkyZone = cls->findProperty(Name("SkyZone"))->offset;
                offsets_zoneinfo.bBounceVelocity = cls->findProperty(Name("bBounceVelocity"))->offset;
                offsets_zoneinfo.bWaterZone = cls->findProperty(Name("bWaterZone"))->offset;
                offsets_zoneinfo.bFogZone = cls->findProperty(Name("bFogZone"))->offset;
                offsets_zoneinfo.bFarClipZone = cls->findProperty(Name("bFarClipZone"))->offset;
                offsets_zoneinfo.bKillZone = cls->findProperty(Name("bKillZone"))->offset;
                offsets_zoneinfo.bNeutralZone = cls->findProperty(Name("bNeutralZone"))->offset;
                offsets_zoneinfo.bGravityZone = cls->findProperty(Name("bGravityZone"))->offset;
                offsets_zoneinfo.bPainZone = cls->findProperty(Name("bPainZone"))->offset;
                offsets_zoneinfo.bDestructive = cls->findProperty(Name("bDestructive"))->offset;
                offsets_zoneinfo.bNoInventory = cls->findProperty(Name("bNoInventory"))->offset;
                offsets_zoneinfo.bMoveProjectiles = cls->findProperty(Name("bMoveProjectiles"))->offset;
                offsets_zoneinfo.bLokiBloodZone = cls->findProperty(Name("bLokiBloodZone"))->offset;
                offsets_zoneinfo.AmbientBrightness = cls->findProperty(Name("AmbientBrightness"))->offset;
                offsets_zoneinfo.AmbientHue = cls->findProperty(Name("AmbientHue"))->offset;
                offsets_zoneinfo.AmbientSaturation = cls->findProperty(Name("AmbientSaturation"))->offset;
                offsets_zoneinfo.FogBrightness = cls->findProperty(Name("FogBrightness"))->offset;
                offsets_zoneinfo.FogHue = cls->findProperty(Name("FogHue"))->offset;
                offsets_zoneinfo.FogSaturation = cls->findProperty(Name("FogSaturation"))->offset;
                offsets_zoneinfo.FogDistance = cls->findProperty(Name("FogDistance"))->offset;
                offsets_zoneinfo.EnvironmentMap = cls->findProperty(Name("EnvironmentMap"))->offset;
                offsets_zoneinfo.TexUPanSpeed = cls->findProperty(Name("TexUPanSpeed"))->offset;
                offsets_zoneinfo.TexVPanSpeed = cls->findProperty(Name("TexVPanSpeed"))->offset;
                offsets_zoneinfo.ViewFlash = cls->findProperty(Name("ViewFlash"))->offset;
                offsets_zoneinfo.ViewFog = cls->findProperty(Name("ViewFog"))->offset;
                offsets_zoneinfo.bReverbZone = cls->findProperty(Name("bReverbZone"))->offset;
                offsets_zoneinfo.bRaytraceReverb = cls->findProperty(Name("bRaytraceReverb"))->offset;
                offsets_zoneinfo.SpeedOfSound = cls->findProperty(Name("SpeedOfSound"))->offset;
                offsets_zoneinfo.MasterGain = cls->findProperty(Name("MasterGain"))->offset;
                offsets_zoneinfo.CutoffHz = cls->findProperty(Name("CutoffHz"))->offset;
                offsets_zoneinfo.Delay = cls->findProperty(Name("Delay"))->offset;
                offsets_zoneinfo.Gain = cls->findProperty(Name("Gain"))->offset;
                offsets_zoneinfo.LensFlare = cls->findProperty(Name("LensFlare"))->offset;
                offsets_zoneinfo.LensFlareOffset = cls->findProperty(Name("LensFlareOffset"))->offset;
                offsets_zoneinfo.LensFlareScale = cls->findProperty(Name("LensFlareScale"))->offset;
                offsets_zoneinfo.MinLightCount = cls->findProperty(Name("MinLightCount"))->offset;
                offsets_zoneinfo.MaxLightCount = cls->findProperty(Name("MaxLightCount"))->offset;
                offsets_zoneinfo.MinLightingPolyCount = cls->findProperty(Name("MinLightingPolyCount"))->offset;
                offsets_zoneinfo.MaxLightingPolyCount = cls->findProperty(Name("MaxLightingPolyCount"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.SkyZoneInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.SkyZoneInfo\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.WarpZoneInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.WarpZoneInfo\n");
            } else {
                offsets_warpzoneinfo.OtherSideURL = cls->findProperty(Name("OtherSideURL"))->offset;
                offsets_warpzoneinfo.ThisTag = cls->findProperty(Name("ThisTag"))->offset;
                offsets_warpzoneinfo.bNoTeleFrag = cls->findProperty(Name("bNoTeleFrag"))->offset;
                offsets_warpzoneinfo.iWarpZone = cls->findProperty(Name("iWarpZone"))->offset;
                offsets_warpzoneinfo.WarpCoords = cls->findProperty(Name("WarpCoords"))->offset;
                offsets_warpzoneinfo.OtherSideActor = cls->findProperty(Name("OtherSideActor"))->offset;
                offsets_warpzoneinfo.OtherSideLevel = cls->findProperty(Name("OtherSideLevel"))->offset;
                offsets_warpzoneinfo.Destinations = cls->findProperty(Name("Destinations"))->offset;
                offsets_warpzoneinfo.numDestinations = cls->findProperty(Name("numDestinations"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.GameInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.GameInfo\n");
            } else {
                offsets_gameinfo.ItemGoals = cls->findProperty(Name("ItemGoals"))->offset;
                offsets_gameinfo.KillGoals = cls->findProperty(Name("KillGoals"))->offset;
                offsets_gameinfo.SecretGoals = cls->findProperty(Name("SecretGoals"))->offset;
                offsets_gameinfo.Difficulty = cls->findProperty(Name("Difficulty"))->offset;
                offsets_gameinfo.bNoMonsters = cls->findProperty(Name("bNoMonsters"))->offset;
                offsets_gameinfo.bMuteSpectators = cls->findProperty(Name("bMuteSpectators"))->offset;
                offsets_gameinfo.bHumansOnly = cls->findProperty(Name("bHumansOnly"))->offset;
                offsets_gameinfo.bRestartLevel = cls->findProperty(Name("bRestartLevel"))->offset;
                offsets_gameinfo.bPauseable = cls->findProperty(Name("bPauseable"))->offset;
                offsets_gameinfo.bCoopWeaponMode = cls->findProperty(Name("bCoopWeaponMode"))->offset;
                offsets_gameinfo.bClassicDeathMessages = cls->findProperty(Name("bClassicDeathMessages"))->offset;
                offsets_gameinfo.bLowGore = cls->findProperty(Name("bLowGore"))->offset;
                offsets_gameinfo.bCanChangeSkin = cls->findProperty(Name("bCanChangeSkin"))->offset;
                offsets_gameinfo.bTeamGame = cls->findProperty(Name("bTeamGame"))->offset;
                offsets_gameinfo.bVeryLowGore = cls->findProperty(Name("bVeryLowGore"))->offset;
                offsets_gameinfo.bNoCheating = cls->findProperty(Name("bNoCheating"))->offset;
                offsets_gameinfo.bAllowFOV = cls->findProperty(Name("bAllowFOV"))->offset;
                offsets_gameinfo.bDeathMatch = cls->findProperty(Name("bDeathMatch"))->offset;
                offsets_gameinfo.bGameEnded = cls->findProperty(Name("bGameEnded"))->offset;
                offsets_gameinfo.bOverTime = cls->findProperty(Name("bOverTime"))->offset;
                offsets_gameinfo.bAlternateMode = cls->findProperty(Name("bAlternateMode"))->offset;
                offsets_gameinfo.bCanViewOthers = cls->findProperty(Name("bCanViewOthers"))->offset;
                offsets_gameinfo.bAllowWeaponDrop = cls->findProperty(Name("bAllowWeaponDrop"))->offset;
                offsets_gameinfo.bAllowShieldDrop = cls->findProperty(Name("bAllowShieldDrop"))->offset;
                offsets_gameinfo.bAutoPickup = cls->findProperty(Name("bAutoPickup"))->offset;
                offsets_gameinfo.bEllipticalMovement = optionalOffset(cls, "bEllipticalMovement");
                offsets_gameinfo.AutoAim = cls->findProperty(Name("AutoAim"))->offset;
                offsets_gameinfo.GameSpeed = cls->findProperty(Name("GameSpeed"))->offset;
                offsets_gameinfo.StartTime = cls->findProperty(Name("StartTime"))->offset;
                offsets_gameinfo.DefaultPlayerClass = cls->findProperty(Name("DefaultPlayerClass"))->offset;
                offsets_gameinfo.DefaultWeapon = cls->findProperty(Name("DefaultWeapon"))->offset;
                offsets_gameinfo.DefaultShield = cls->findProperty(Name("DefaultShield"))->offset;
                offsets_gameinfo.MaxSpectators = cls->findProperty(Name("MaxSpectators"))->offset;
                offsets_gameinfo.NumSpectators = cls->findProperty(Name("NumSpectators"))->offset;
                offsets_gameinfo.AdminPassword = cls->findProperty(Name("AdminPassword"))->offset;
                offsets_gameinfo.GamePassword = cls->findProperty(Name("GamePassword"))->offset;
                offsets_gameinfo.ScoreBoardType = cls->findProperty(Name("ScoreBoardType"))->offset;
                offsets_gameinfo.GameMenuType = cls->findProperty(Name("GameMenuType"))->offset;
                offsets_gameinfo.BotMenuType = cls->findProperty(Name("BotMenuType"))->offset;
                offsets_gameinfo.RulesMenuType = cls->findProperty(Name("RulesMenuType"))->offset;
                offsets_gameinfo.SettingsMenuType = cls->findProperty(Name("SettingsMenuType"))->offset;
                offsets_gameinfo.MutatorMenuType = cls->findProperty(Name("MutatorMenuType"))->offset;
                offsets_gameinfo.MaplistMenuType = cls->findProperty(Name("MaplistMenuType"))->offset;
                offsets_gameinfo.GameUMenuType = cls->findProperty(Name("GameUMenuType"))->offset;
                offsets_gameinfo.MultiplayerUMenuType = cls->findProperty(Name("MultiplayerUMenuType"))->offset;
                offsets_gameinfo.GameOptionsMenuType = cls->findProperty(Name("GameOptionsMenuType"))->offset;
                offsets_gameinfo.HUDType = cls->findProperty(Name("HUDType"))->offset;
                offsets_gameinfo.MapListType = cls->findProperty(Name("MapListType"))->offset;
                offsets_gameinfo.MapPrefix = cls->findProperty(Name("MapPrefix"))->offset;
                offsets_gameinfo.BeaconName = cls->findProperty(Name("BeaconName"))->offset;
                offsets_gameinfo.SpecialDamageString = cls->findProperty(Name("SpecialDamageString"))->offset;
                offsets_gameinfo.SwitchLevelMessage = cls->findProperty(Name("SwitchLevelMessage"))->offset;
                offsets_gameinfo.SentText = cls->findProperty(Name("SentText"))->offset;
                offsets_gameinfo.DefaultPlayerName = cls->findProperty(Name("DefaultPlayerName"))->offset;
                offsets_gameinfo.LeftMessage = cls->findProperty(Name("LeftMessage"))->offset;
                offsets_gameinfo.FailedSpawnMessage = cls->findProperty(Name("FailedSpawnMessage"))->offset;
                offsets_gameinfo.FailedPlaceMessage = cls->findProperty(Name("FailedPlaceMessage"))->offset;
                offsets_gameinfo.FailedTeamMessage = cls->findProperty(Name("FailedTeamMessage"))->offset;
                offsets_gameinfo.NameChangedMessage = cls->findProperty(Name("NameChangedMessage"))->offset;
                offsets_gameinfo.EnteredMessage = cls->findProperty(Name("EnteredMessage"))->offset;
                offsets_gameinfo.GameName = cls->findProperty(Name("GameName"))->offset;
                offsets_gameinfo.MaxedOutMessage = cls->findProperty(Name("MaxedOutMessage"))->offset;
                offsets_gameinfo.WrongPassword = cls->findProperty(Name("WrongPassword"))->offset;
                offsets_gameinfo.NeedPassword = cls->findProperty(Name("NeedPassword"))->offset;
                offsets_gameinfo.IPBanned = cls->findProperty(Name("IPBanned"))->offset;
                offsets_gameinfo.MaxPlayers = cls->findProperty(Name("MaxPlayers"))->offset;
                offsets_gameinfo.NumPlayers = cls->findProperty(Name("NumPlayers"))->offset;
                offsets_gameinfo.CurrentID = cls->findProperty(Name("CurrentID"))->offset;
                offsets_gameinfo.IPPolicies = cls->findProperty(Name("IPPolicies"))->offset;
                offsets_gameinfo.DeathMessageClass = cls->findProperty(Name("DeathMessageClass"))->offset;
                offsets_gameinfo.DMMessageClass = cls->findProperty(Name("DMMessageClass"))->offset;
                offsets_gameinfo.MutatorClass = cls->findProperty(Name("MutatorClass"))->offset;
                offsets_gameinfo.BaseMutator = cls->findProperty(Name("BaseMutator"))->offset;
                offsets_gameinfo.DamageMutator = cls->findProperty(Name("DamageMutator"))->offset;
                offsets_gameinfo.WaterZoneType = cls->findProperty(Name("WaterZoneType"))->offset;
                offsets_gameinfo.DefaultPlayerState = cls->findProperty(Name("DefaultPlayerState"))->offset;
                offsets_gameinfo.GameReplicationInfoClass = cls->findProperty(Name("GameReplicationInfoClass"))->offset;
                offsets_gameinfo.GameReplicationInfo = cls->findProperty(Name("GameReplicationInfo"))->offset;
                offsets_gameinfo.ServerLogName = cls->findProperty(Name("ServerLogName"))->offset;
                offsets_gameinfo.LocalLog = cls->findProperty(Name("LocalLog"))->offset;
                offsets_gameinfo.WorldLog = cls->findProperty(Name("WorldLog"))->offset;
                offsets_gameinfo.bLocalLog = cls->findProperty(Name("bLocalLog"))->offset;
                offsets_gameinfo.bWorldLog = cls->findProperty(Name("bWorldLog"))->offset;
                offsets_gameinfo.bBatchLocal = cls->findProperty(Name("bBatchLocal"))->offset;
                offsets_gameinfo.bSubtitles = cls->findProperty(Name("bSubtitles"))->offset;
                offsets_gameinfo.bLoggingGame = cls->findProperty(Name("bLoggingGame"))->offset;
                offsets_gameinfo.LocalLogFileName = cls->findProperty(Name("LocalLogFileName"))->offset;
                offsets_gameinfo.WorldLogFileName = cls->findProperty(Name("WorldLogFileName"))->offset;
                offsets_gameinfo.StatLogClass = cls->findProperty(Name("StatLogClass"))->offset;
                offsets_gameinfo.DemoBuild = cls->findProperty(Name("DemoBuild"))->offset;
                offsets_gameinfo.DemoHasTuts = cls->findProperty(Name("DemoHasTuts"))->offset;
                offsets_gameinfo.DebrisPercentage = cls->findProperty(Name("DebrisPercentage"))->offset;
                offsets_gameinfo.ParticlePercentage = cls->findProperty(Name("ParticlePercentage"))->offset;
                offsets_gameinfo.bAllowLimbSever = optionalOffset(cls, "bAllowLimbSever");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Brush");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Brush\n");
            } else {
                offsets_brush.CsgOper = cls->findProperty(Name("CsgOper"))->offset;
                offsets_brush.UnusedLightMesh = cls->findProperty(Name("UnusedLightMesh"))->offset;
                offsets_brush.PostPivot = cls->findProperty(Name("PostPivot"))->offset;
                offsets_brush.MainScale = cls->findProperty(Name("MainScale"))->offset;
                offsets_brush.PostScale = cls->findProperty(Name("PostScale"))->offset;
                offsets_brush.TempScale = cls->findProperty(Name("TempScale"))->offset;
                offsets_brush.BrushColor = cls->findProperty(Name("BrushColor"))->offset;
                offsets_brush.PolyFlags = cls->findProperty(Name("PolyFlags"))->offset;
                offsets_brush.bColored = cls->findProperty(Name("bColored"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Mover");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Mover\n");
            } else {
                offsets_mover.MoverEncroachType = cls->findProperty(Name("MoverEncroachType"))->offset;
                offsets_mover.MoverGlideType = cls->findProperty(Name("MoverGlideType"))->offset;
                offsets_mover.BumpType = cls->findProperty(Name("BumpType"))->offset;
                offsets_mover.KeyNum = cls->findProperty(Name("KeyNum"))->offset;
                offsets_mover.PrevKeyNum = cls->findProperty(Name("PrevKeyNum"))->offset;
                offsets_mover.NumKeys = cls->findProperty(Name("NumKeys"))->offset;
                offsets_mover.WorldRaytraceKey = cls->findProperty(Name("WorldRaytraceKey"))->offset;
                offsets_mover.BrushRaytraceKey = cls->findProperty(Name("BrushRaytraceKey"))->offset;
                offsets_mover.MoveTime = cls->findProperty(Name("MoveTime"))->offset;
                offsets_mover.StayOpenTime = cls->findProperty(Name("StayOpenTime"))->offset;
                offsets_mover.OtherTime = cls->findProperty(Name("OtherTime"))->offset;
                offsets_mover.EncroachDamage = cls->findProperty(Name("EncroachDamage"))->offset;
                offsets_mover.InterpolateEvent = cls->findProperty(Name("InterpolateEvent"))->offset;
                offsets_mover.bTriggerOnceOnly = cls->findProperty(Name("bTriggerOnceOnly"))->offset;
                offsets_mover.bSlave = cls->findProperty(Name("bSlave"))->offset;
                offsets_mover.bUseTriggered = cls->findProperty(Name("bUseTriggered"))->offset;
                offsets_mover.bDamageTriggered = cls->findProperty(Name("bDamageTriggered"))->offset;
                offsets_mover.bDynamicLightMover = cls->findProperty(Name("bDynamicLightMover"))->offset;
                offsets_mover.PlayerBumpEvent = cls->findProperty(Name("PlayerBumpEvent"))->offset;
                offsets_mover.BumpEvent = cls->findProperty(Name("BumpEvent"))->offset;
                offsets_mover.SavedTrigger = cls->findProperty(Name("SavedTrigger"))->offset;
                offsets_mover.DamageThreshold = cls->findProperty(Name("DamageThreshold"))->offset;
                offsets_mover.numTriggerEvents = cls->findProperty(Name("numTriggerEvents"))->offset;
                offsets_mover.Leader = cls->findProperty(Name("Leader"))->offset;
                offsets_mover.Follower = cls->findProperty(Name("Follower"))->offset;
                offsets_mover.ReturnGroup = cls->findProperty(Name("ReturnGroup"))->offset;
                offsets_mover.DelayTime = cls->findProperty(Name("DelayTime"))->offset;
                offsets_mover.OpeningSound = cls->findProperty(Name("OpeningSound"))->offset;
                offsets_mover.OpenedSound = cls->findProperty(Name("OpenedSound"))->offset;
                offsets_mover.ClosingSound = cls->findProperty(Name("ClosingSound"))->offset;
                offsets_mover.ClosedSound = cls->findProperty(Name("ClosedSound"))->offset;
                offsets_mover.MoveAmbientSound = cls->findProperty(Name("MoveAmbientSound"))->offset;
                offsets_mover.KeyPos = cls->findProperty(Name("KeyPos"))->offset;
                offsets_mover.KeyRot = cls->findProperty(Name("KeyRot"))->offset;
                offsets_mover.BasePos = cls->findProperty(Name("BasePos"))->offset;
                offsets_mover.OldPos = cls->findProperty(Name("OldPos"))->offset;
                offsets_mover.OldPrePivot = cls->findProperty(Name("OldPrePivot"))->offset;
                offsets_mover.SavedPos = cls->findProperty(Name("SavedPos"))->offset;
                offsets_mover.BaseRot = cls->findProperty(Name("BaseRot"))->offset;
                offsets_mover.OldRot = cls->findProperty(Name("OldRot"))->offset;
                offsets_mover.SavedRot = cls->findProperty(Name("SavedRot"))->offset;
                offsets_mover.myMarker = cls->findProperty(Name("myMarker"))->offset;
                offsets_mover.TriggerActor = cls->findProperty(Name("TriggerActor"))->offset;
                offsets_mover.TriggerActor2 = cls->findProperty(Name("TriggerActor2"))->offset;
                offsets_mover.WaitingPawn = cls->findProperty(Name("WaitingPawn"))->offset;
                offsets_mover.bOpening = cls->findProperty(Name("bOpening"))->offset;
                offsets_mover.bDelaying = cls->findProperty(Name("bDelaying"))->offset;
                offsets_mover.bClientPause = cls->findProperty(Name("bClientPause"))->offset;
                offsets_mover.bPlayerOnly = cls->findProperty(Name("bPlayerOnly"))->offset;
                offsets_mover.RecommendedTrigger = cls->findProperty(Name("RecommendedTrigger"))->offset;
                offsets_mover.SimOldPos = cls->findProperty(Name("SimOldPos"))->offset;
                offsets_mover.SimOldRotPitch = cls->findProperty(Name("SimOldRotPitch"))->offset;
                offsets_mover.SimOldRotYaw = cls->findProperty(Name("SimOldRotYaw"))->offset;
                offsets_mover.SimOldRotRoll = cls->findProperty(Name("SimOldRotRoll"))->offset;
                offsets_mover.SimInterpolate = cls->findProperty(Name("SimInterpolate"))->offset;
                offsets_mover.RealPosition = cls->findProperty(Name("RealPosition"))->offset;
                offsets_mover.RealRotation = cls->findProperty(Name("RealRotation"))->offset;
                offsets_mover.ClientUpdate = cls->findProperty(Name("ClientUpdate"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Polyobj");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Polyobj\n");
            } else {
                offsets_polyobj.PolyObjGlideType = cls->findProperty(Name("PolyObjGlideType"))->offset;
                offsets_polyobj.KeyNum = cls->findProperty(Name("KeyNum"))->offset;
                offsets_polyobj.KeyPos = cls->findProperty(Name("KeyPos"))->offset;
                offsets_polyobj.KeyRot = cls->findProperty(Name("KeyRot"))->offset;
                offsets_polyobj.BasePos = cls->findProperty(Name("BasePos"))->offset;
                offsets_polyobj.OldPos = cls->findProperty(Name("OldPos"))->offset;
                offsets_polyobj.BaseRot = cls->findProperty(Name("BaseRot"))->offset;
                offsets_polyobj.OldRot = cls->findProperty(Name("OldRot"))->offset;
                offsets_polyobj.bPlayerOnly = cls->findProperty(Name("bPlayerOnly"))->offset;
                offsets_polyobj.bDynamicLightPolyobj = cls->findProperty(Name("bDynamicLightPolyobj"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.NavigationPoint");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.NavigationPoint\n");
            } else {
                offsets_navigationpoint.ownerTeam = cls->findProperty(Name("ownerTeam"))->offset;
                offsets_navigationpoint.taken = cls->findProperty(Name("taken"))->offset;
                offsets_navigationpoint.upstreamPaths = cls->findProperty(Name("upstreamPaths"))->offset;
                offsets_navigationpoint.Paths = cls->findProperty(Name("Paths"))->offset;
                offsets_navigationpoint.PrunedPaths = cls->findProperty(Name("PrunedPaths"))->offset;
                offsets_navigationpoint.VisNoReachPaths = cls->findProperty(Name("VisNoReachPaths"))->offset;
                offsets_navigationpoint.visitedWeight = cls->findProperty(Name("visitedWeight"))->offset;
                offsets_navigationpoint.RouteCache = cls->findProperty(Name("RouteCache"))->offset;
                offsets_navigationpoint.bestPathWeight = cls->findProperty(Name("bestPathWeight"))->offset;
                offsets_navigationpoint.nextNavigationPoint = cls->findProperty(Name("nextNavigationPoint"))->offset;
                offsets_navigationpoint.nextOrdered = cls->findProperty(Name("nextOrdered"))->offset;
                offsets_navigationpoint.prevOrdered = cls->findProperty(Name("prevOrdered"))->offset;
                offsets_navigationpoint.startPath = cls->findProperty(Name("startPath"))->offset;
                offsets_navigationpoint.previousPath = cls->findProperty(Name("previousPath"))->offset;
                offsets_navigationpoint.cost = cls->findProperty(Name("cost"))->offset;
                offsets_navigationpoint.ExtraCost = cls->findProperty(Name("ExtraCost"))->offset;
                offsets_navigationpoint.bPlayerOnly = cls->findProperty(Name("bPlayerOnly"))->offset;
                offsets_navigationpoint.bEndPoint = cls->findProperty(Name("bEndPoint"))->offset;
                offsets_navigationpoint.bEndPointOnly = cls->findProperty(Name("bEndPointOnly"))->offset;
                offsets_navigationpoint.bSpecialCost = cls->findProperty(Name("bSpecialCost"))->offset;
                offsets_navigationpoint.bOneWayPath = cls->findProperty(Name("bOneWayPath"))->offset;
                offsets_navigationpoint.bNeverUseStrafing = cls->findProperty(Name("bNeverUseStrafing"))->offset;
                offsets_navigationpoint.bAutoBuilt = cls->findProperty(Name("bAutoBuilt"))->offset;
                offsets_navigationpoint.bTwoWay = cls->findProperty(Name("bTwoWay"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Inventory");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Inventory\n");
            } else {
                offsets_inventory.AutoSwitchPriority = cls->findProperty(Name("AutoSwitchPriority"))->offset;
                offsets_inventory.InventoryGroup = cls->findProperty(Name("InventoryGroup"))->offset;
                offsets_inventory.bActivatable = cls->findProperty(Name("bActivatable"))->offset;
                offsets_inventory.bDisplayableInv = cls->findProperty(Name("bDisplayableInv"))->offset;
                offsets_inventory.bActive = cls->findProperty(Name("bActive"))->offset;
                offsets_inventory.bSleepTouch = cls->findProperty(Name("bSleepTouch"))->offset;
                offsets_inventory.bHeldItem = cls->findProperty(Name("bHeldItem"))->offset;
                offsets_inventory.bTossedOut = cls->findProperty(Name("bTossedOut"))->offset;
                offsets_inventory.bExpireWhenTossed = cls->findProperty(Name("bExpireWhenTossed"))->offset;
                offsets_inventory.bAmbientGlow = cls->findProperty(Name("bAmbientGlow"))->offset;
                offsets_inventory.bInstantRespawn = cls->findProperty(Name("bInstantRespawn"))->offset;
                offsets_inventory.bRotatingPickup = cls->findProperty(Name("bRotatingPickup"))->offset;
                offsets_inventory.PickupMessage = cls->findProperty(Name("PickupMessage"))->offset;
                offsets_inventory.ItemName = cls->findProperty(Name("ItemName"))->offset;
                offsets_inventory.ItemArticle = cls->findProperty(Name("ItemArticle"))->offset;
                offsets_inventory.RespawnTime = cls->findProperty(Name("RespawnTime"))->offset;
                offsets_inventory.PlayerLastTouched = cls->findProperty(Name("PlayerLastTouched"))->offset;
                offsets_inventory.PlayerViewOffset = cls->findProperty(Name("PlayerViewOffset"))->offset;
                offsets_inventory.PlayerViewMesh = cls->findProperty(Name("PlayerViewMesh"))->offset;
                offsets_inventory.PlayerViewScale = cls->findProperty(Name("PlayerViewScale"))->offset;
                offsets_inventory.BobDamping = cls->findProperty(Name("BobDamping"))->offset;
                offsets_inventory.ExpireTime = cls->findProperty(Name("ExpireTime"))->offset;
                offsets_inventory.PickupViewMesh = cls->findProperty(Name("PickupViewMesh"))->offset;
                offsets_inventory.PickupViewScale = cls->findProperty(Name("PickupViewScale"))->offset;
                offsets_inventory.ThirdPersonMesh = cls->findProperty(Name("ThirdPersonMesh"))->offset;
                offsets_inventory.ThirdPersonScale = cls->findProperty(Name("ThirdPersonScale"))->offset;
                offsets_inventory.StatusIcon = cls->findProperty(Name("StatusIcon"))->offset;
                offsets_inventory.ProtectionType1 = cls->findProperty(Name("ProtectionType1"))->offset;
                offsets_inventory.ProtectionType2 = cls->findProperty(Name("ProtectionType2"))->offset;
                offsets_inventory.Charge = cls->findProperty(Name("Charge"))->offset;
                offsets_inventory.ArmorAbsorption = cls->findProperty(Name("ArmorAbsorption"))->offset;
                offsets_inventory.bIsAnArmor = cls->findProperty(Name("bIsAnArmor"))->offset;
                offsets_inventory.AbsorptionPriority = cls->findProperty(Name("AbsorptionPriority"))->offset;
                offsets_inventory.NextArmor = cls->findProperty(Name("NextArmor"))->offset;
                offsets_inventory.MaxDesireability = cls->findProperty(Name("MaxDesireability"))->offset;
                offsets_inventory.myMarker = cls->findProperty(Name("myMarker"))->offset;
                offsets_inventory.bSteadyFlash3rd = cls->findProperty(Name("bSteadyFlash3rd"))->offset;
                offsets_inventory.bFirstFrame = cls->findProperty(Name("bFirstFrame"))->offset;
                offsets_inventory.bMuzzleFlashParticles = cls->findProperty(Name("bMuzzleFlashParticles"))->offset;
                offsets_inventory.bToggleSteadyFlash = cls->findProperty(Name("bToggleSteadyFlash"))->offset;
                offsets_inventory.bSteadyToggle = cls->findProperty(Name("bSteadyToggle"))->offset;
                offsets_inventory.FlashCount = cls->findProperty(Name("FlashCount"))->offset;
                offsets_inventory.OldFlashCount = cls->findProperty(Name("OldFlashCount"))->offset;
                offsets_inventory.MuzzleFlashStyle = cls->findProperty(Name("MuzzleFlashStyle"))->offset;
                offsets_inventory.MuzzleFlashMesh = cls->findProperty(Name("MuzzleFlashMesh"))->offset;
                offsets_inventory.MuzzleFlashScale = cls->findProperty(Name("MuzzleFlashScale"))->offset;
                offsets_inventory.MuzzleFlashTexture = cls->findProperty(Name("MuzzleFlashTexture"))->offset;
                offsets_inventory.PickupSound = cls->findProperty(Name("PickupSound"))->offset;
                offsets_inventory.ActivateSound = cls->findProperty(Name("ActivateSound"))->offset;
                offsets_inventory.DeActivateSound = cls->findProperty(Name("DeActivateSound"))->offset;
                offsets_inventory.RespawnSound = cls->findProperty(Name("RespawnSound"))->offset;
                offsets_inventory.DropSound = cls->findProperty(Name("DropSound"))->offset;
                offsets_inventory.Icon = cls->findProperty(Name("Icon"))->offset;
                offsets_inventory.M_Activated = cls->findProperty(Name("M_Activated"))->offset;
                offsets_inventory.M_Selected = cls->findProperty(Name("M_Selected"))->offset;
                offsets_inventory.M_Deactivated = cls->findProperty(Name("M_Deactivated"))->offset;
                offsets_inventory.PickupMessageClass = cls->findProperty(Name("PickupMessageClass"))->offset;
                offsets_inventory.ItemMessageClass = cls->findProperty(Name("ItemMessageClass"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Weapon");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Weapon\n");
            } else {
                offsets_weapon.MeleeType = cls->findProperty(Name("MeleeType"))->offset;
                offsets_weapon.bWeaponStay = cls->findProperty(Name("bWeaponStay"))->offset;
                offsets_weapon.bCrouchTwoHands = cls->findProperty(Name("bCrouchTwoHands"))->offset;
                offsets_weapon.bPoweredUp = cls->findProperty(Name("bPoweredUp"))->offset;
                offsets_weapon.bCanBePoweredUp = cls->findProperty(Name("bCanBePoweredUp"))->offset;
                offsets_weapon.bPlayedDropSound = cls->findProperty(Name("bPlayedDropSound"))->offset;
                offsets_weapon.bClientPoweredUp = cls->findProperty(Name("bClientPoweredUp"))->offset;
                offsets_weapon.HitMatterSoundCount = cls->findProperty(Name("HitMatterSoundCount"))->offset;
                offsets_weapon.StowMesh = cls->findProperty(Name("StowMesh"))->offset;
                offsets_weapon.Damage = cls->findProperty(Name("Damage"))->offset;
                offsets_weapon.DamageType = cls->findProperty(Name("DamageType"))->offset;
                offsets_weapon.ThrownDamageType = cls->findProperty(Name("ThrownDamageType"))->offset;
                offsets_weapon.BloodTexture = cls->findProperty(Name("BloodTexture"))->offset;
                offsets_weapon.rating = cls->findProperty(Name("rating"))->offset;
                offsets_weapon.WeaponSweepExtent = cls->findProperty(Name("WeaponSweepExtent"))->offset;
                offsets_weapon.SweepJoint1 = cls->findProperty(Name("SweepJoint1"))->offset;
                offsets_weapon.SweepJoint2 = cls->findProperty(Name("SweepJoint2"))->offset;
                offsets_weapon.ExtendedLength = cls->findProperty(Name("ExtendedLength"))->offset;
                offsets_weapon.RunePowerRequired = cls->findProperty(Name("RunePowerRequired"))->offset;
                offsets_weapon.RunePowerDuration = cls->findProperty(Name("RunePowerDuration"))->offset;
                offsets_weapon.PowerupMessage = cls->findProperty(Name("PowerupMessage"))->offset;
                offsets_weapon.StabMesh = cls->findProperty(Name("StabMesh"))->offset;
                offsets_weapon.TimerCount = cls->findProperty(Name("TimerCount"))->offset;
                offsets_weapon.SweepVector = cls->findProperty(Name("SweepVector"))->offset;
                offsets_weapon.FrameOfAttackAnim = cls->findProperty(Name("FrameOfAttackAnim"))->offset;
                offsets_weapon.gB1 = cls->findProperty(Name("gB1"))->offset;
                offsets_weapon.gE1 = cls->findProperty(Name("gE1"))->offset;
                offsets_weapon.gB2 = cls->findProperty(Name("gB2"))->offset;
                offsets_weapon.gE2 = cls->findProperty(Name("gE2"))->offset;
                offsets_weapon.StabbedActor = cls->findProperty(Name("StabbedActor"))->offset;
                offsets_weapon.LastThrower = optionalOffset(cls, "LastThrower");
                offsets_weapon.ThroughAir = cls->findProperty(Name("ThroughAir"))->offset;
                offsets_weapon.ThroughAirBerserk = cls->findProperty(Name("ThroughAirBerserk"))->offset;
                offsets_weapon.HitFlesh = cls->findProperty(Name("HitFlesh"))->offset;
                offsets_weapon.HitWood = cls->findProperty(Name("HitWood"))->offset;
                offsets_weapon.HitStone = cls->findProperty(Name("HitStone"))->offset;
                offsets_weapon.HitMetal = cls->findProperty(Name("HitMetal"))->offset;
                offsets_weapon.HitDirt = cls->findProperty(Name("HitDirt"))->offset;
                offsets_weapon.HitShield = cls->findProperty(Name("HitShield"))->offset;
                offsets_weapon.HitWeapon = cls->findProperty(Name("HitWeapon"))->offset;
                offsets_weapon.HitBreakableWood = cls->findProperty(Name("HitBreakableWood"))->offset;
                offsets_weapon.HitBreakableStone = cls->findProperty(Name("HitBreakableStone"))->offset;
                offsets_weapon.SheathSound = cls->findProperty(Name("SheathSound"))->offset;
                offsets_weapon.UnsheathSound = cls->findProperty(Name("UnsheathSound"))->offset;
                offsets_weapon.ThrownSoundLOOP = cls->findProperty(Name("ThrownSoundLOOP"))->offset;
                offsets_weapon.PowerUpSound = cls->findProperty(Name("PowerUpSound"))->offset;
                offsets_weapon.PoweredUpSoundLOOP = cls->findProperty(Name("PoweredUpSoundLOOP"))->offset;
                offsets_weapon.PoweredUpEndingSound = cls->findProperty(Name("PoweredUpEndingSound"))->offset;
                offsets_weapon.PoweredUpEndSound = cls->findProperty(Name("PoweredUpEndSound"))->offset;
                offsets_weapon.PitchDeviation = cls->findProperty(Name("PitchDeviation"))->offset;
                offsets_weapon.PowerupIcon = cls->findProperty(Name("PowerupIcon"))->offset;
                offsets_weapon.PowerupIconAnim = cls->findProperty(Name("PowerupIconAnim"))->offset;
                offsets_weapon.lastpos1 = cls->findProperty(Name("lastpos1"))->offset;
                offsets_weapon.lastpos2 = cls->findProperty(Name("lastpos2"))->offset;
                offsets_weapon.NumThroughAirSounds = cls->findProperty(Name("NumThroughAirSounds"))->offset;
                offsets_weapon.NumThroughAirBerserkSounds = cls->findProperty(Name("NumThroughAirBerserkSounds"))->offset;
                offsets_weapon.NumFleshSounds = cls->findProperty(Name("NumFleshSounds"))->offset;
                offsets_weapon.NumWoodSounds = cls->findProperty(Name("NumWoodSounds"))->offset;
                offsets_weapon.NumStoneSounds = cls->findProperty(Name("NumStoneSounds"))->offset;
                offsets_weapon.NumMetalSounds = cls->findProperty(Name("NumMetalSounds"))->offset;
                offsets_weapon.NumEarthSounds = cls->findProperty(Name("NumEarthSounds"))->offset;
                offsets_weapon.SwipeHits = cls->findProperty(Name("SwipeHits"))->offset;
                offsets_weapon.swipe = cls->findProperty(Name("swipe"))->offset;
                offsets_weapon.SwipeClass = cls->findProperty(Name("SwipeClass"))->offset;
                offsets_weapon.PoweredUpSwipeClass = cls->findProperty(Name("PoweredUpSwipeClass"))->offset;
                offsets_weapon.A_Idle = cls->findProperty(Name("A_Idle"))->offset;
                offsets_weapon.A_TurnLeft = cls->findProperty(Name("A_TurnLeft"))->offset;
                offsets_weapon.A_TurnRight = cls->findProperty(Name("A_TurnRight"))->offset;
                offsets_weapon.A_Forward = cls->findProperty(Name("A_Forward"))->offset;
                offsets_weapon.A_Backward = cls->findProperty(Name("A_Backward"))->offset;
                offsets_weapon.A_Forward45Right = cls->findProperty(Name("A_Forward45Right"))->offset;
                offsets_weapon.A_Forward45Left = cls->findProperty(Name("A_Forward45Left"))->offset;
                offsets_weapon.A_Backward45Right = cls->findProperty(Name("A_Backward45Right"))->offset;
                offsets_weapon.A_Backward45Left = cls->findProperty(Name("A_Backward45Left"))->offset;
                offsets_weapon.A_StrafeRight = cls->findProperty(Name("A_StrafeRight"))->offset;
                offsets_weapon.A_StrafeLeft = cls->findProperty(Name("A_StrafeLeft"))->offset;
                offsets_weapon.A_Jump = cls->findProperty(Name("A_Jump"))->offset;
                offsets_weapon.A_ForwardAttack = cls->findProperty(Name("A_ForwardAttack"))->offset;
                offsets_weapon.A_AttackA = cls->findProperty(Name("A_AttackA"))->offset;
                offsets_weapon.A_AttackAReturn = cls->findProperty(Name("A_AttackAReturn"))->offset;
                offsets_weapon.A_AttackB = cls->findProperty(Name("A_AttackB"))->offset;
                offsets_weapon.A_AttackBReturn = cls->findProperty(Name("A_AttackBReturn"))->offset;
                offsets_weapon.A_AttackC = cls->findProperty(Name("A_AttackC"))->offset;
                offsets_weapon.A_AttackCReturn = cls->findProperty(Name("A_AttackCReturn"))->offset;
                offsets_weapon.A_AttackD = cls->findProperty(Name("A_AttackD"))->offset;
                offsets_weapon.A_AttackDReturn = cls->findProperty(Name("A_AttackDReturn"))->offset;
                offsets_weapon.A_AttackStandA = cls->findProperty(Name("A_AttackStandA"))->offset;
                offsets_weapon.A_AttackStandAReturn = cls->findProperty(Name("A_AttackStandAReturn"))->offset;
                offsets_weapon.A_AttackStandB = cls->findProperty(Name("A_AttackStandB"))->offset;
                offsets_weapon.A_AttackStandBReturn = cls->findProperty(Name("A_AttackStandBReturn"))->offset;
                offsets_weapon.A_AttackBackupA = cls->findProperty(Name("A_AttackBackupA"))->offset;
                offsets_weapon.A_AttackBackupAReturn = cls->findProperty(Name("A_AttackBackupAReturn"))->offset;
                offsets_weapon.A_AttackBackupB = cls->findProperty(Name("A_AttackBackupB"))->offset;
                offsets_weapon.A_AttackBackupBReturn = cls->findProperty(Name("A_AttackBackupBReturn"))->offset;
                offsets_weapon.A_AttackStrafeRight = cls->findProperty(Name("A_AttackStrafeRight"))->offset;
                offsets_weapon.A_AttackStrafeLeft = cls->findProperty(Name("A_AttackStrafeLeft"))->offset;
                offsets_weapon.A_JumpAttack = cls->findProperty(Name("A_JumpAttack"))->offset;
                offsets_weapon.A_Throw = cls->findProperty(Name("A_Throw"))->offset;
                offsets_weapon.A_Powerup = cls->findProperty(Name("A_Powerup"))->offset;
                offsets_weapon.A_Defend = cls->findProperty(Name("A_Defend"))->offset;
                offsets_weapon.A_DefendIdle = cls->findProperty(Name("A_DefendIdle"))->offset;
                offsets_weapon.A_PainFront = cls->findProperty(Name("A_PainFront"))->offset;
                offsets_weapon.A_PainBack = cls->findProperty(Name("A_PainBack"))->offset;
                offsets_weapon.A_PainLeft = cls->findProperty(Name("A_PainLeft"))->offset;
                offsets_weapon.A_PainRight = cls->findProperty(Name("A_PainRight"))->offset;
                offsets_weapon.A_PickupGroundLeft = cls->findProperty(Name("A_PickupGroundLeft"))->offset;
                offsets_weapon.A_PickupHighLeft = cls->findProperty(Name("A_PickupHighLeft"))->offset;
                offsets_weapon.A_Taunt = cls->findProperty(Name("A_Taunt"))->offset;
                offsets_weapon.A_PumpTrigger = cls->findProperty(Name("A_PumpTrigger"))->offset;
                offsets_weapon.A_LeverTrigger = cls->findProperty(Name("A_LeverTrigger"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Projectile");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Projectile\n");
            } else {
                offsets_projectile.speed = cls->findProperty(Name("speed"))->offset;
                offsets_projectile.MaxSpeed = cls->findProperty(Name("MaxSpeed"))->offset;
                offsets_projectile.Damage = cls->findProperty(Name("Damage"))->offset;
                offsets_projectile.MomentumTransfer = cls->findProperty(Name("MomentumTransfer"))->offset;
                offsets_projectile.MyDamageType = cls->findProperty(Name("MyDamageType"))->offset;
                offsets_projectile.SpawnSound = cls->findProperty(Name("SpawnSound"))->offset;
                offsets_projectile.ImpactSound = cls->findProperty(Name("ImpactSound"))->offset;
                offsets_projectile.MiscSound = cls->findProperty(Name("MiscSound"))->offset;
                offsets_projectile.ExploWallOut = cls->findProperty(Name("ExploWallOut"))->offset;
                offsets_projectile.ExplosionDecal = cls->findProperty(Name("ExplosionDecal"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Decoration");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Decoration\n");
            } else {
                offsets_decoration.EffectWhenDestroyed = cls->findProperty(Name("EffectWhenDestroyed"))->offset;
                offsets_decoration.bPushable = cls->findProperty(Name("bPushable"))->offset;
                offsets_decoration.bOnlyTriggerable = cls->findProperty(Name("bOnlyTriggerable"))->offset;
                offsets_decoration.bSplash = cls->findProperty(Name("bSplash"))->offset;
                offsets_decoration.bBobbing = cls->findProperty(Name("bBobbing"))->offset;
                offsets_decoration.bWasCarried = cls->findProperty(Name("bWasCarried"))->offset;
                offsets_decoration.PushSound = cls->findProperty(Name("PushSound"))->offset;
                offsets_decoration.numLandings = cls->findProperty(Name("numLandings"))->offset;
                offsets_decoration.contents = cls->findProperty(Name("contents"))->offset;
                offsets_decoration.content2 = cls->findProperty(Name("content2"))->offset;
                offsets_decoration.content3 = cls->findProperty(Name("content3"))->offset;
                offsets_decoration.EndPushSound = cls->findProperty(Name("EndPushSound"))->offset;
                offsets_decoration.bPushSoundPlaying = cls->findProperty(Name("bPushSoundPlaying"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Canvas");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Canvas\n");
            } else {
                offsets_canvas.Font = cls->findProperty(Name("Font"))->offset;
                offsets_canvas.SpaceX = cls->findProperty(Name("SpaceX"))->offset;
                offsets_canvas.SpaceY = cls->findProperty(Name("SpaceY"))->offset;
                offsets_canvas.OrgX = cls->findProperty(Name("OrgX"))->offset;
                offsets_canvas.OrgY = cls->findProperty(Name("OrgY"))->offset;
                offsets_canvas.ClipX = cls->findProperty(Name("ClipX"))->offset;
                offsets_canvas.ClipY = cls->findProperty(Name("ClipY"))->offset;
                offsets_canvas.CurX = cls->findProperty(Name("CurX"))->offset;
                offsets_canvas.CurY = cls->findProperty(Name("CurY"))->offset;
                offsets_canvas.Z = cls->findProperty(Name("Z"))->offset;
                offsets_canvas.Style = cls->findProperty(Name("Style"))->offset;
                offsets_canvas.CurYL = cls->findProperty(Name("CurYL"))->offset;
                offsets_canvas.DrawColor = cls->findProperty(Name("DrawColor"))->offset;
                offsets_canvas.bCenter = cls->findProperty(Name("bCenter"))->offset;
                offsets_canvas.bNoSmooth = cls->findProperty(Name("bNoSmooth"))->offset;
                offsets_canvas.SizeX = cls->findProperty(Name("SizeX"))->offset;
                offsets_canvas.SizeY = cls->findProperty(Name("SizeY"))->offset;
                offsets_canvas.AlphaScale = cls->findProperty(Name("AlphaScale"))->offset;
                offsets_canvas.SmallFont = cls->findProperty(Name("SmallFont"))->offset;
                offsets_canvas.MedFont = cls->findProperty(Name("MedFont"))->offset;
                offsets_canvas.BigFont = cls->findProperty(Name("BigFont"))->offset;
                offsets_canvas.LargeFont = cls->findProperty(Name("LargeFont"))->offset;
                offsets_canvas.RuneMedFont = optionalOffset(cls, "RuneMedFont");
                offsets_canvas.CredsFont = optionalOffset(cls, "CredsFont");
                offsets_canvas.ButtonFont = optionalOffset(cls, "ButtonFont");
                offsets_canvas.Viewport = cls->findProperty(Name("Viewport"))->offset;
                offsets_canvas.FramePtr = cls->findProperty(Name("FramePtr"))->offset;
                offsets_canvas.RenderPtr = cls->findProperty(Name("RenderPtr"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Console");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Console\n");
            } else {
                offsets_console.vtblOut = cls->findProperty(Name("vtblOut"))->offset;
                offsets_console.Viewport = cls->findProperty(Name("Viewport"))->offset;
                offsets_console.HistoryTop = cls->findProperty(Name("HistoryTop"))->offset;
                offsets_console.HistoryBot = cls->findProperty(Name("HistoryBot"))->offset;
                offsets_console.HistoryCur = cls->findProperty(Name("HistoryCur"))->offset;
                offsets_console.TypedStr = cls->findProperty(Name("TypedStr"))->offset;
                offsets_console.History = cls->findProperty(Name("History"))->offset;
                offsets_console.Scrollback = cls->findProperty(Name("Scrollback"))->offset;
                offsets_console.numLines = cls->findProperty(Name("numLines"))->offset;
                offsets_console.TopLine = cls->findProperty(Name("TopLine"))->offset;
                offsets_console.TextLines = cls->findProperty(Name("TextLines"))->offset;
                offsets_console.MsgTime = cls->findProperty(Name("MsgTime"))->offset;
                offsets_console.MsgTickTime = cls->findProperty(Name("MsgTickTime"))->offset;
                offsets_console.MsgText = cls->findProperty(Name("MsgText"))->offset;
                offsets_console.MsgType = cls->findProperty(Name("MsgType"))->offset;
                offsets_console.MsgPlayer = cls->findProperty(Name("MsgPlayer"))->offset;
                offsets_console.MsgTick = cls->findProperty(Name("MsgTick"))->offset;
                offsets_console.BorderSize = cls->findProperty(Name("BorderSize"))->offset;
                offsets_console.ConsoleLines = cls->findProperty(Name("ConsoleLines"))->offset;
                offsets_console.BorderLines = cls->findProperty(Name("BorderLines"))->offset;
                offsets_console.BorderPixels = cls->findProperty(Name("BorderPixels"))->offset;
                offsets_console.ConsolePos = cls->findProperty(Name("ConsolePos"))->offset;
                offsets_console.ConsoleDest = cls->findProperty(Name("ConsoleDest"))->offset;
                offsets_console.FrameX = cls->findProperty(Name("FrameX"))->offset;
                offsets_console.FrameY = cls->findProperty(Name("FrameY"))->offset;
                offsets_console.ConBackground = cls->findProperty(Name("ConBackground"))->offset;
                offsets_console.Border = cls->findProperty(Name("Border"))->offset;
                offsets_console.bNoStuff = cls->findProperty(Name("bNoStuff"))->offset;
                offsets_console.bTyping = cls->findProperty(Name("bTyping"))->offset;
                offsets_console.bNoDrawWorld = cls->findProperty(Name("bNoDrawWorld"))->offset;
                offsets_console.bTimeDemo = cls->findProperty(Name("bTimeDemo"))->offset;
                offsets_console.bStartTimeDemo = cls->findProperty(Name("bStartTimeDemo"))->offset;
                offsets_console.bRestartTimeDemo = cls->findProperty(Name("bRestartTimeDemo"))->offset;
                offsets_console.bSaveTimeDemoToFile = cls->findProperty(Name("bSaveTimeDemoToFile"))->offset;
                offsets_console.StartTime = cls->findProperty(Name("StartTime"))->offset;
                offsets_console.ExtraTime = cls->findProperty(Name("ExtraTime"))->offset;
                offsets_console.LastFrameTime = cls->findProperty(Name("LastFrameTime"))->offset;
                offsets_console.LastSecondStartTime = cls->findProperty(Name("LastSecondStartTime"))->offset;
                offsets_console.FrameCount = cls->findProperty(Name("FrameCount"))->offset;
                offsets_console.LastSecondFrameCount = cls->findProperty(Name("LastSecondFrameCount"))->offset;
                offsets_console.MinFPS = cls->findProperty(Name("MinFPS"))->offset;
                offsets_console.MaxFPS = cls->findProperty(Name("MaxFPS"))->offset;
                offsets_console.LastSecFPS = cls->findProperty(Name("LastSecFPS"))->offset;
                offsets_console.TimeDemoFont = cls->findProperty(Name("TimeDemoFont"))->offset;
                offsets_console.LoadingMessage = cls->findProperty(Name("LoadingMessage"))->offset;
                offsets_console.SavingMessage = cls->findProperty(Name("SavingMessage"))->offset;
                offsets_console.ConnectingMessage = cls->findProperty(Name("ConnectingMessage"))->offset;
                offsets_console.PausedMessage = cls->findProperty(Name("PausedMessage"))->offset;
                offsets_console.PrecachingMessage = cls->findProperty(Name("PrecachingMessage"))->offset;
                offsets_console.FrameRateText = cls->findProperty(Name("FrameRateText"))->offset;
                offsets_console.AvgText = cls->findProperty(Name("AvgText"))->offset;
                offsets_console.LastSecText = cls->findProperty(Name("LastSecText"))->offset;
                offsets_console.MinText = cls->findProperty(Name("MinText"))->offset;
                offsets_console.MaxText = cls->findProperty(Name("MaxText"))->offset;
                offsets_console.fpsText = cls->findProperty(Name("fpsText"))->offset;
                offsets_console.SecondsText = cls->findProperty(Name("SecondsText"))->offset;
                offsets_console.FramesText = cls->findProperty(Name("FramesText"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.HUD");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.HUD\n");
            } else {
                offsets_hud.HudMode = cls->findProperty(Name("HudMode"))->offset;
                offsets_hud.Crosshair = cls->findProperty(Name("Crosshair"))->offset;
                offsets_hud.MainMenuType = cls->findProperty(Name("MainMenuType"))->offset;
                offsets_hud.HUDConfigWindowType = cls->findProperty(Name("HUDConfigWindowType"))->offset;
                offsets_hud.WhiteColor = cls->findProperty(Name("WhiteColor"))->offset;
                offsets_hud.MainMenu = cls->findProperty(Name("MainMenu"))->offset;
                offsets_hud.HUDMutator = cls->findProperty(Name("HUDMutator"))->offset;
                offsets_hud.PlayerOwner = cls->findProperty(Name("PlayerOwner"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Bitmap");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Bitmap\n");
            } else {
                offsets_bitmap.Format = cls->findProperty(Name("Format"))->offset;
                offsets_bitmap.Palette = cls->findProperty(Name("Palette"))->offset;
                offsets_bitmap.UBits = cls->findProperty(Name("UBits"))->offset;
                offsets_bitmap.VBits = cls->findProperty(Name("VBits"))->offset;
                offsets_bitmap.USize = cls->findProperty(Name("USize"))->offset;
                offsets_bitmap.VSize = cls->findProperty(Name("VSize"))->offset;
                offsets_bitmap.UClamp = cls->findProperty(Name("UClamp"))->offset;
                offsets_bitmap.VClamp = cls->findProperty(Name("VClamp"))->offset;
                offsets_bitmap.MipZero = cls->findProperty(Name("MipZero"))->offset;
                offsets_bitmap.MaxColor = cls->findProperty(Name("MaxColor"))->offset;
                offsets_bitmap.InternalTime = cls->findProperty(Name("InternalTime"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Texture");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Texture\n");
            } else {
                offsets_texture.BumpMap = cls->findProperty(Name("BumpMap"))->offset;
                offsets_texture.DetailTexture = cls->findProperty(Name("DetailTexture"))->offset;
                offsets_texture.MacroTexture = cls->findProperty(Name("MacroTexture"))->offset;
                offsets_texture.Diffuse = cls->findProperty(Name("Diffuse"))->offset;
                offsets_texture.Specular = cls->findProperty(Name("Specular"))->offset;
                offsets_texture.Alpha = cls->findProperty(Name("Alpha"))->offset;
                offsets_texture.DrawScale = cls->findProperty(Name("DrawScale"))->offset;
                offsets_texture.Friction = cls->findProperty(Name("Friction"))->offset;
                offsets_texture.MipMult = cls->findProperty(Name("MipMult"))->offset;
                offsets_texture.FootstepSound = cls->findProperty(Name("FootstepSound"))->offset;
                offsets_texture.HitSound = cls->findProperty(Name("HitSound"))->offset;
                offsets_texture.bInvisible = cls->findProperty(Name("bInvisible"))->offset;
                offsets_texture.bMasked = cls->findProperty(Name("bMasked"))->offset;
                offsets_texture.bTransparent = cls->findProperty(Name("bTransparent"))->offset;
                offsets_texture.bNotSolid = cls->findProperty(Name("bNotSolid"))->offset;
                offsets_texture.bEnvironment = cls->findProperty(Name("bEnvironment"))->offset;
                offsets_texture.bSemisolid = cls->findProperty(Name("bSemisolid"))->offset;
                offsets_texture.bModulate = cls->findProperty(Name("bModulate"))->offset;
                offsets_texture.bFakeBackdrop = cls->findProperty(Name("bFakeBackdrop"))->offset;
                offsets_texture.bTwoSided = cls->findProperty(Name("bTwoSided"))->offset;
                offsets_texture.bAutoUPan = cls->findProperty(Name("bAutoUPan"))->offset;
                offsets_texture.bAutoVPan = cls->findProperty(Name("bAutoVPan"))->offset;
                offsets_texture.bNoSmooth = cls->findProperty(Name("bNoSmooth"))->offset;
                offsets_texture.bBigWavy = cls->findProperty(Name("bBigWavy"))->offset;
                offsets_texture.bSmallWavy = cls->findProperty(Name("bSmallWavy"))->offset;
                offsets_texture.bWaterWavy = cls->findProperty(Name("bWaterWavy"))->offset;
                offsets_texture.bLowShadowDetail = cls->findProperty(Name("bLowShadowDetail"))->offset;
                offsets_texture.bNoMerge = cls->findProperty(Name("bNoMerge"))->offset;
                offsets_texture.bCloudWavy = cls->findProperty(Name("bCloudWavy"))->offset;
                offsets_texture.bDirtyShadows = cls->findProperty(Name("bDirtyShadows"))->offset;
                offsets_texture.bHighLedge = cls->findProperty(Name("bHighLedge"))->offset;
                offsets_texture.bSpecialLit = cls->findProperty(Name("bSpecialLit"))->offset;
                offsets_texture.bGouraud = cls->findProperty(Name("bGouraud"))->offset;
                offsets_texture.bUnlit = cls->findProperty(Name("bUnlit"))->offset;
                offsets_texture.bHighShadowDetail = cls->findProperty(Name("bHighShadowDetail"))->offset;
                offsets_texture.bPortal = cls->findProperty(Name("bPortal"))->offset;
                offsets_texture.bMirrored = cls->findProperty(Name("bMirrored"))->offset;
                offsets_texture.bX2 = cls->findProperty(Name("bX2"))->offset;
                offsets_texture.bX3 = cls->findProperty(Name("bX3"))->offset;
                offsets_texture.bX4 = cls->findProperty(Name("bX4"))->offset;
                offsets_texture.bX5 = cls->findProperty(Name("bX5"))->offset;
                offsets_texture.bX6 = cls->findProperty(Name("bX6"))->offset;
                offsets_texture.bX7 = cls->findProperty(Name("bX7"))->offset;
                offsets_texture.bHighColorQuality = cls->findProperty(Name("bHighColorQuality"))->offset;
                offsets_texture.bHighTextureQuality = cls->findProperty(Name("bHighTextureQuality"))->offset;
                offsets_texture.bRealtime = cls->findProperty(Name("bRealtime"))->offset;
                offsets_texture.bParametric = cls->findProperty(Name("bParametric"))->offset;
                offsets_texture.bRealtimeChanged = cls->findProperty(Name("bRealtimeChanged"))->offset;
                offsets_texture.bHasComp = cls->findProperty(Name("bHasComp"))->offset;
                offsets_texture.LODSet = cls->findProperty(Name("LODSet"))->offset;
                offsets_texture.AnimNext = cls->findProperty(Name("AnimNext"))->offset;
                offsets_texture.AnimCurrent = cls->findProperty(Name("AnimCurrent"))->offset;
                offsets_texture.PrimeCount = cls->findProperty(Name("PrimeCount"))->offset;
                offsets_texture.PrimeCurrent = cls->findProperty(Name("PrimeCurrent"))->offset;
                offsets_texture.MinFrameRate = cls->findProperty(Name("MinFrameRate"))->offset;
                offsets_texture.MaxFrameRate = cls->findProperty(Name("MaxFrameRate"))->offset;
                offsets_texture.Accumulator = cls->findProperty(Name("Accumulator"))->offset;
                offsets_texture.Mips = cls->findProperty(Name("Mips"))->offset;
                offsets_texture.CompMips = cls->findProperty(Name("CompMips"))->offset;
                offsets_texture.CompFormat = cls->findProperty(Name("CompFormat"))->offset;
                offsets_texture.TextureMaterial = cls->findProperty(Name("TextureMaterial"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Player");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Player\n");
            } else {
                offsets_player.vfOut = cls->findProperty(Name("vfOut"))->offset;
                offsets_player.vfExec = cls->findProperty(Name("vfExec"))->offset;
                offsets_player.Actor = cls->findProperty(Name("Actor"))->offset;
                offsets_player.Console = cls->findProperty(Name("Console"))->offset;
                offsets_player.bWindowsMouseAvailable = cls->findProperty(Name("bWindowsMouseAvailable"))->offset;
                offsets_player.bShowWindowsMouse = cls->findProperty(Name("bShowWindowsMouse"))->offset;
                offsets_player.bSuspendPrecaching = cls->findProperty(Name("bSuspendPrecaching"))->offset;
                offsets_player.WindowsMouseX = cls->findProperty(Name("WindowsMouseX"))->offset;
                offsets_player.WindowsMouseY = cls->findProperty(Name("WindowsMouseY"))->offset;
                offsets_player.CurrentNetSpeed = cls->findProperty(Name("CurrentNetSpeed"))->offset;
                offsets_player.ConfiguredInternetSpeed = cls->findProperty(Name("ConfiguredInternetSpeed"))->offset;
                offsets_player.ConfiguredLanSpeed = cls->findProperty(Name("ConfiguredLanSpeed"))->offset;
                offsets_player.SelectedCursor = cls->findProperty(Name("SelectedCursor"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Light");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Light\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Info");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Info\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.PlayerReplicationInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.PlayerReplicationInfo\n");
            } else {
                offsets_playerreplicationinfo.PlayerName = cls->findProperty(Name("PlayerName"))->offset;
                offsets_playerreplicationinfo.OldName = cls->findProperty(Name("OldName"))->offset;
                offsets_playerreplicationinfo.PlayerID = cls->findProperty(Name("PlayerID"))->offset;
                offsets_playerreplicationinfo.TeamName = cls->findProperty(Name("TeamName"))->offset;
                offsets_playerreplicationinfo.Team = cls->findProperty(Name("Team"))->offset;
                offsets_playerreplicationinfo.TeamID = cls->findProperty(Name("TeamID"))->offset;
                offsets_playerreplicationinfo.Score = cls->findProperty(Name("Score"))->offset;
                offsets_playerreplicationinfo.Deaths = cls->findProperty(Name("Deaths"))->offset;
                offsets_playerreplicationinfo.VoiceType = cls->findProperty(Name("VoiceType"))->offset;
                offsets_playerreplicationinfo.HasFlag = cls->findProperty(Name("HasFlag"))->offset;
                offsets_playerreplicationinfo.Ping = cls->findProperty(Name("Ping"))->offset;
                offsets_playerreplicationinfo.PacketLoss = cls->findProperty(Name("PacketLoss"))->offset;
                offsets_playerreplicationinfo.bIsFemale = cls->findProperty(Name("bIsFemale"))->offset;
                offsets_playerreplicationinfo.bIsABot = cls->findProperty(Name("bIsABot"))->offset;
                offsets_playerreplicationinfo.bFeigningDeath = cls->findProperty(Name("bFeigningDeath"))->offset;
                offsets_playerreplicationinfo.bIsSpectator = cls->findProperty(Name("bIsSpectator"))->offset;
                offsets_playerreplicationinfo.bWaitingPlayer = cls->findProperty(Name("bWaitingPlayer"))->offset;
                offsets_playerreplicationinfo.bAdmin = cls->findProperty(Name("bAdmin"))->offset;
                offsets_playerreplicationinfo.bReadyToPlay = cls->findProperty(Name("bReadyToPlay"))->offset;
                offsets_playerreplicationinfo.bFirstBlood = cls->findProperty(Name("bFirstBlood"))->offset;
                offsets_playerreplicationinfo.TalkTexture = cls->findProperty(Name("TalkTexture"))->offset;
                offsets_playerreplicationinfo.PlayerZone = cls->findProperty(Name("PlayerZone"))->offset;
                offsets_playerreplicationinfo.PlayerLocation = cls->findProperty(Name("PlayerLocation"))->offset;
                offsets_playerreplicationinfo.MaxSpree = cls->findProperty(Name("MaxSpree"))->offset;
                offsets_playerreplicationinfo.HeadKills = cls->findProperty(Name("HeadKills"))->offset;
                offsets_playerreplicationinfo.StartTime = cls->findProperty(Name("StartTime"))->offset;
                offsets_playerreplicationinfo.TimeAcc = cls->findProperty(Name("TimeAcc"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Decal");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Decal\n");
            } else {
                offsets_decal.MultiDecalLevel = cls->findProperty(Name("MultiDecalLevel"))->offset;
                offsets_decal.LastRenderedTime = cls->findProperty(Name("LastRenderedTime"))->offset;
                offsets_decal.bBloodyDecal = cls->findProperty(Name("bBloodyDecal"))->offset;
                offsets_decal.SurfList = cls->findProperty(Name("SurfList"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Teleporter");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Teleporter\n");
            } else {
                offsets_teleporter.URL = cls->findProperty(Name("URL"))->offset;
                offsets_teleporter.ProductRequired = cls->findProperty(Name("ProductRequired"))->offset;
                offsets_teleporter.bChangesVelocity = cls->findProperty(Name("bChangesVelocity"))->offset;
                offsets_teleporter.bChangesYaw = cls->findProperty(Name("bChangesYaw"))->offset;
                offsets_teleporter.bReversesX = cls->findProperty(Name("bReversesX"))->offset;
                offsets_teleporter.bReversesY = cls->findProperty(Name("bReversesY"))->offset;
                offsets_teleporter.bReversesZ = cls->findProperty(Name("bReversesZ"))->offset;
                offsets_teleporter.bEnabled = cls->findProperty(Name("bEnabled"))->offset;
                offsets_teleporter.TargetVelocity = cls->findProperty(Name("TargetVelocity"))->offset;
                offsets_teleporter.TriggerActor = cls->findProperty(Name("TriggerActor"))->offset;
                offsets_teleporter.TriggerActor2 = cls->findProperty(Name("TriggerActor2"))->offset;
                offsets_teleporter.LastFired = cls->findProperty(Name("LastFired"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.ScriptedTexture");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.ScriptedTexture\n");
            } else {
                offsets_scriptedtexture.NotifyActor = cls->findProperty(Name("NotifyActor"))->offset;
                offsets_scriptedtexture.SourceTexture = cls->findProperty(Name("SourceTexture"))->offset;
                offsets_scriptedtexture.Junk1 = cls->findProperty(Name("Junk1"))->offset;
                offsets_scriptedtexture.Junk2 = cls->findProperty(Name("Junk2"))->offset;
                offsets_scriptedtexture.Junk3 = cls->findProperty(Name("Junk3"))->offset;
                offsets_scriptedtexture.LocalTime = cls->findProperty(Name("LocalTime"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Carcass");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Carcass\n");
            } else {
                offsets_carcass.bPlayerCarcass = cls->findProperty(Name("bPlayerCarcass"))->offset;
                offsets_carcass.flies = cls->findProperty(Name("flies"))->offset;
                offsets_carcass.rats = cls->findProperty(Name("rats"))->offset;
                offsets_carcass.bReducedHeight = cls->findProperty(Name("bReducedHeight"))->offset;
                offsets_carcass.bDecorative = cls->findProperty(Name("bDecorative"))->offset;
                offsets_carcass.bSlidingCarcass = cls->findProperty(Name("bSlidingCarcass"))->offset;
                offsets_carcass.CumulativeDamage = cls->findProperty(Name("CumulativeDamage"))->offset;
                offsets_carcass.PlayerOwner = cls->findProperty(Name("PlayerOwner"))->offset;
                offsets_carcass.Bugs = cls->findProperty(Name("Bugs"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Pickup");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Pickup\n");
            } else {
                offsets_pickup.Inv = cls->findProperty(Name("Inv"))->offset;
                offsets_pickup.NumCopies = cls->findProperty(Name("NumCopies"))->offset;
                offsets_pickup.bCanHaveMultipleCopies = cls->findProperty(Name("bCanHaveMultipleCopies"))->offset;
                offsets_pickup.bCanActivate = cls->findProperty(Name("bCanActivate"))->offset;
                offsets_pickup.ExpireMessage = cls->findProperty(Name("ExpireMessage"))->offset;
                offsets_pickup.bAutoActivate = cls->findProperty(Name("bAutoActivate"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.PathNode");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.PathNode\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Trigger");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Trigger\n");
            } else {
                offsets_trigger.TriggerType = cls->findProperty(Name("TriggerType"))->offset;
                offsets_trigger.Message = cls->findProperty(Name("Message"))->offset;
                offsets_trigger.bTriggerOnceOnly = cls->findProperty(Name("bTriggerOnceOnly"))->offset;
                offsets_trigger.bInitiallyActive = cls->findProperty(Name("bInitiallyActive"))->offset;
                offsets_trigger.ClassProximityType = cls->findProperty(Name("ClassProximityType"))->offset;
                offsets_trigger.RepeatTriggerTime = cls->findProperty(Name("RepeatTriggerTime"))->offset;
                offsets_trigger.ReTriggerDelay = cls->findProperty(Name("ReTriggerDelay"))->offset;
                offsets_trigger.TriggerTime = cls->findProperty(Name("TriggerTime"))->offset;
                offsets_trigger.DamageThreshold = cls->findProperty(Name("DamageThreshold"))->offset;
                offsets_trigger.SightDistance = cls->findProperty(Name("SightDistance"))->offset;
                offsets_trigger.SightAngle = cls->findProperty(Name("SightAngle"))->offset;
                offsets_trigger.TriggerActor = cls->findProperty(Name("TriggerActor"))->offset;
                offsets_trigger.TriggerActor2 = cls->findProperty(Name("TriggerActor2"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Effects");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Effects\n");
            } else {
                offsets_effects.EffectSound1 = cls->findProperty(Name("EffectSound1"))->offset;
                offsets_effects.EffectSound2 = cls->findProperty(Name("EffectSound2"))->offset;
                offsets_effects.bOnlyTriggerable = cls->findProperty(Name("bOnlyTriggerable"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.SpawnNotify");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.SpawnNotify\n");
            } else {
                offsets_spawnnotify.ActorClass = cls->findProperty(Name("ActorClass"))->offset;
                offsets_spawnnotify.Next = cls->findProperty(Name("Next"))->offset;
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Keypoint");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Keypoint\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Mesh");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Mesh\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.ParticleSystem");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.ParticleSystem\n");
            } else {
                offsets_particlesystem.IsLoaded = cls->findProperty(Name("IsLoaded"))->offset;
                offsets_particlesystem.bSpriteInEditor = cls->findProperty(Name("bSpriteInEditor"))->offset;
                offsets_particlesystem.bSystemOneShot = cls->findProperty(Name("bSystemOneShot"))->offset;
                offsets_particlesystem.bSystemTicks = cls->findProperty(Name("bSystemTicks"))->offset;
                offsets_particlesystem.bRelativeToSystem = cls->findProperty(Name("bRelativeToSystem"))->offset;
                offsets_particlesystem.bEventDeath = cls->findProperty(Name("bEventDeath"))->offset;
                offsets_particlesystem.ParticleCount = cls->findProperty(Name("ParticleCount"))->offset;
                offsets_particlesystem.ParticleTexture = cls->findProperty(Name("ParticleTexture"))->offset;
                offsets_particlesystem.bRandomTexture = cls->findProperty(Name("bRandomTexture"))->offset;
                offsets_particlesystem.ParticleType = cls->findProperty(Name("ParticleType"))->offset;
                offsets_particlesystem.ParticleSpriteType = cls->findProperty(Name("ParticleSpriteType"))->offset;
                offsets_particlesystem.SpawnShape = cls->findProperty(Name("SpawnShape"))->offset;
                offsets_particlesystem.RandomDelay = cls->findProperty(Name("RandomDelay"))->offset;
                offsets_particlesystem.SystemLifeSpan = cls->findProperty(Name("SystemLifeSpan"))->offset;
                offsets_particlesystem.CurrentDelay = cls->findProperty(Name("CurrentDelay"))->offset;
                offsets_particlesystem.OldParticleCount = cls->findProperty(Name("OldParticleCount"))->offset;
                offsets_particlesystem.SystemCoords = cls->findProperty(Name("SystemCoords"))->offset;
                offsets_particlesystem.HasValidCoords = cls->findProperty(Name("HasValidCoords"))->offset;
                offsets_particlesystem.LastTime = cls->findProperty(Name("LastTime"))->offset;
                offsets_particlesystem.CurrentTime = cls->findProperty(Name("CurrentTime"))->offset;
                offsets_particlesystem.ParticleArray = cls->findProperty(Name("ParticleArray"))->offset;
                offsets_particlesystem.OriginOffset = cls->findProperty(Name("OriginOffset"))->offset;
                offsets_particlesystem.ShapeVector = cls->findProperty(Name("ShapeVector"))->offset;
                offsets_particlesystem.VelocityMin = cls->findProperty(Name("VelocityMin"))->offset;
                offsets_particlesystem.VelocityMax = cls->findProperty(Name("VelocityMax"))->offset;
                offsets_particlesystem.ScaleMin = cls->findProperty(Name("ScaleMin"))->offset;
                offsets_particlesystem.ScaleMax = cls->findProperty(Name("ScaleMax"))->offset;
                offsets_particlesystem.ScaleDeltaX = cls->findProperty(Name("ScaleDeltaX"))->offset;
                offsets_particlesystem.ScaleDeltaY = cls->findProperty(Name("ScaleDeltaY"))->offset;
                offsets_particlesystem.LifeSpanMin = cls->findProperty(Name("LifeSpanMin"))->offset;
                offsets_particlesystem.LifeSpanMax = cls->findProperty(Name("LifeSpanMax"))->offset;
                offsets_particlesystem.AlphaStart = cls->findProperty(Name("AlphaStart"))->offset;
                offsets_particlesystem.AlphaEnd = cls->findProperty(Name("AlphaEnd"))->offset;
                offsets_particlesystem.PercentOffset = cls->findProperty(Name("PercentOffset"))->offset;
                offsets_particlesystem.bAlphaFade = cls->findProperty(Name("bAlphaFade"))->offset;
                offsets_particlesystem.bApplyGravity = cls->findProperty(Name("bApplyGravity"))->offset;
                offsets_particlesystem.GravityScale = cls->findProperty(Name("GravityScale"))->offset;
                offsets_particlesystem.bApplyZoneVelocity = cls->findProperty(Name("bApplyZoneVelocity"))->offset;
                offsets_particlesystem.ZoneVelocityScale = cls->findProperty(Name("ZoneVelocityScale"))->offset;
                offsets_particlesystem.bWaterOnly = cls->findProperty(Name("bWaterOnly"))->offset;
                offsets_particlesystem.bOneShot = cls->findProperty(Name("bOneShot"))->offset;
                offsets_particlesystem.bConvergeX = cls->findProperty(Name("bConvergeX"))->offset;
                offsets_particlesystem.bConvergeY = cls->findProperty(Name("bConvergeY"))->offset;
                offsets_particlesystem.bConvergeZ = cls->findProperty(Name("bConvergeZ"))->offset;
                offsets_particlesystem.bConstrainToBounds = cls->findProperty(Name("bConstrainToBounds"))->offset;
                offsets_particlesystem.SpawnDelay = cls->findProperty(Name("SpawnDelay"))->offset;
                offsets_particlesystem.SpawnOverTime = cls->findProperty(Name("SpawnOverTime"))->offset;
                offsets_particlesystem.TextureChangeTime = cls->findProperty(Name("TextureChangeTime"))->offset;
                offsets_particlesystem.BaseJointIndex = cls->findProperty(Name("BaseJointIndex"))->offset;
                offsets_particlesystem.OffsetJointIndex = cls->findProperty(Name("OffsetJointIndex"))->offset;
                offsets_particlesystem.OldBaseLocation = cls->findProperty(Name("OldBaseLocation"))->offset;
                offsets_particlesystem.OldOffsetLocation = cls->findProperty(Name("OldOffsetLocation"))->offset;
                offsets_particlesystem.SwipeSpeed = cls->findProperty(Name("SwipeSpeed"))->offset;
                offsets_particlesystem.NumConPts = cls->findProperty(Name("NumConPts"))->offset;
                offsets_particlesystem.BeamThickness = cls->findProperty(Name("BeamThickness"))->offset;
                offsets_particlesystem.BeamTextureScale = cls->findProperty(Name("BeamTextureScale"))->offset;
                offsets_particlesystem.TargetJointIndex = cls->findProperty(Name("TargetJointIndex"))->offset;
                offsets_particlesystem.ConnectionPoint = cls->findProperty(Name("ConnectionPoint"))->offset;
                offsets_particlesystem.ConnectionOffset = cls->findProperty(Name("ConnectionOffset"))->offset;
                offsets_particlesystem.bUseTargetLocation = cls->findProperty(Name("bUseTargetLocation"))->offset;
                offsets_particlesystem.TargetLocation = cls->findProperty(Name("TargetLocation"))->offset;
                offsets_particlesystem.bEventSystemInit = cls->findProperty(Name("bEventSystemInit"))->offset;
                offsets_particlesystem.bEventSystemTick = cls->findProperty(Name("bEventSystemTick"))->offset;
                offsets_particlesystem.bEventParticleTick = cls->findProperty(Name("bEventParticleTick"))->offset;
                offsets_particlesystem.bTaperStartPoint = cls->findProperty(Name("bTaperStartPoint"))->offset;
                offsets_particlesystem.bTaperEndPoint = cls->findProperty(Name("bTaperEndPoint"))->offset;
            }
        }
    }
}
