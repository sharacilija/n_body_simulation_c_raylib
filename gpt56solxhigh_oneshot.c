/*
    N-BODY COSMIC SANDBOX
    ---------------------
    A single-file raylib project.

    Controls
    --------
    1-5             Load a preset
    R               Restart the current preset
    Space           Pause / resume
    N               Advance one physics step while paused
    Left drag       Launch a new body
    Shift + drag    Launch a massive body
    Right drag      Pan the camera
    Mouse wheel     Zoom toward the cursor
    C               Center on the system's center of mass
    F               Follow the most massive body
    T               Toggle trails
    V               Toggle gravity-field visualization
    B               Toggle velocity vectors
    M               Toggle collisions
    - / =           Change simulation speed
    [ / ]           Change gravity
    H               Toggle the full help panel
    P               Save a screenshot
    F11             Toggle fullscreen
*/

#include <raylib.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INITIAL_WIDTH        1280
#define INITIAL_HEIGHT       820

#define MAX_BODIES           1400
#define MAX_PARTICLES        2200
#define MAX_SHOCKWAVES       64
#define TRAIL_POINTS         72
#define BACKGROUND_STARS     280

#define FIXED_DT             (1.0 / 120.0)
#define MAX_PHYSICS_STEPS    12
#define TRAIL_INTERVAL       0.045
#define DEFAULT_GRAVITY      35.0
#define MIN_ZOOM             0.12f
#define MAX_ZOOM             6.0f
#define TAU                   6.28318530717958647692

typedef struct {
    double x;
    double y;
} Vec2d;

typedef struct {
    Vec2d pos;
    Vec2d vel;
    Vec2d acc;

    double mass;
    double radius;
    Color color;
    bool active;

    Vec2d trail[TRAIL_POINTS];
    int trailHead;
    int trailCount;
} Body;

typedef struct {
    Vec2d pos;
    Vec2d vel;
    float life;
    float maxLife;
    float size;
    Color color;
    bool active;
} Particle;

typedef struct {
    Vec2d pos;
    float radius;
    float speed;
    float life;
    float maxLife;
    Color color;
    bool active;
} Shockwave;

typedef struct {
    float x;
    float y;
    float size;
    float depth;
    float phase;
    Color color;
} BackgroundStar;

typedef struct {
    bool paused;
    bool showTrails;
    bool showField;
    bool showVelocity;
    bool collisions;
    bool showHelp;
    bool followMassive;
    bool launching;
    bool screenshotRequested;

    Vec2d launchStart;
    double gravity;
    double timeScale;
    double trailClock;
    int preset;
} SimulationState;

static Body gBodies[MAX_BODIES];
static Particle gParticles[MAX_PARTICLES];
static Shockwave gShockwaves[MAX_SHOCKWAVES];
static BackgroundStar gStars[BACKGROUND_STARS];

static int gBodySlots = 0;
static int gParticleCursor = 0;
static int gShockwaveCursor = 0;
static SimulationState gState;

static const Color PALETTE[] = {
    {  96, 165, 250, 255 },   /* blue */
    { 129, 230, 217, 255 },   /* cyan */
    { 196, 181, 253, 255 },   /* violet */
    { 244, 114, 182, 255 },   /* pink */
    { 251, 146,  60, 255 },   /* orange */
    { 253, 224,  71, 255 },   /* yellow */
    { 134, 239, 172, 255 },   /* green */
    { 248, 250, 252, 255 }    /* white */
};

#define PALETTE_COUNT ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

/* -------------------------------------------------------------------------- */
/* Small math and color helpers                                               */
/* -------------------------------------------------------------------------- */

static double RandomDouble(double min, double max)
{
    return min + ((double)rand() / (double)RAND_MAX) * (max - min);
}

static double ClampDouble(double value, double min, double max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static float ClampFloat(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static Vec2d V2d(double x, double y)
{
    Vec2d value = { x, y };
    return value;
}

static Vec2d V2dAdd(Vec2d a, Vec2d b)
{
    return V2d(a.x + b.x, a.y + b.y);
}

static Vec2d V2dSub(Vec2d a, Vec2d b)
{
    return V2d(a.x - b.x, a.y - b.y);
}

static Vec2d V2dScale(Vec2d value, double scale)
{
    return V2d(value.x * scale, value.y * scale);
}

static double V2dLength(Vec2d value)
{
    return sqrt(value.x * value.x + value.y * value.y);
}

static Vec2d V2dNormalize(Vec2d value)
{
    double length = V2dLength(value);
    if (length < 0.0000001) return V2d(0.0, 0.0);
    return V2dScale(value, 1.0 / length);
}

static Vec2d V2dRotate(Vec2d value, double angle)
{
    double c = cos(angle);
    double s = sin(angle);
    return V2d(value.x * c - value.y * s,
               value.x * s + value.y * c);
}

static Vector2 ToVector2(Vec2d value)
{
    return (Vector2){ (float)value.x, (float)value.y };
}

static Vec2d FromVector2(Vector2 value)
{
    return V2d((double)value.x, (double)value.y);
}

static Color MixColors(Color a, Color b, double amountOfB)
{
    double t = ClampDouble(amountOfB, 0.0, 1.0);
    Color result = {
        (unsigned char)(a.r * (1.0 - t) + b.r * t),
        (unsigned char)(a.g * (1.0 - t) + b.g * t),
        (unsigned char)(a.b * (1.0 - t) + b.b * t),
        255
    };
    return result;
}

static Color RandomPaletteColor(void)
{
    return PALETTE[rand() % PALETTE_COUNT];
}

/* -------------------------------------------------------------------------- */
/* Bodies, trails, particles, and collision effects                           */
/* -------------------------------------------------------------------------- */

static void ClearTrail(Body *body)
{
    body->trailHead = 0;
    body->trailCount = 0;
}

static void PushTrailPoint(Body *body)
{
    body->trail[body->trailHead] = body->pos;
    body->trailHead = (body->trailHead + 1) % TRAIL_POINTS;

    if (body->trailCount < TRAIL_POINTS) {
        body->trailCount++;
    }
}

static Body *AddBody(Vec2d pos, Vec2d vel, double radius,
                     double mass, Color color)
{
    int slot = -1;

    if (gBodySlots < MAX_BODIES) {
        slot = gBodySlots++;
    } else {
        for (int i = 0; i < gBodySlots; i++) {
            if (!gBodies[i].active) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) return NULL;

    Body *body = &gBodies[slot];
    memset(body, 0, sizeof(*body));

    body->pos = pos;
    body->vel = vel;
    body->radius = radius;
    body->mass = mass;
    body->color = color;
    body->active = true;

    PushTrailPoint(body);
    return body;
}

static void SpawnParticle(Vec2d pos, Vec2d vel, float life,
                          float size, Color color)
{
    Particle *particle = &gParticles[gParticleCursor];
    gParticleCursor = (gParticleCursor + 1) % MAX_PARTICLES;

    particle->pos = pos;
    particle->vel = vel;
    particle->life = life;
    particle->maxLife = life;
    particle->size = size;
    particle->color = color;
    particle->active = true;
}

static void SpawnShockwave(Vec2d pos, float speed, float life, Color color)
{
    Shockwave *shockwave = &gShockwaves[gShockwaveCursor];
    gShockwaveCursor = (gShockwaveCursor + 1) % MAX_SHOCKWAVES;

    shockwave->pos = pos;
    shockwave->radius = 1.0f;
    shockwave->speed = speed;
    shockwave->life = life;
    shockwave->maxLife = life;
    shockwave->color = color;
    shockwave->active = true;
}

static void SpawnMergeEffects(Vec2d pos, Vec2d baseVelocity,
                              double mergedRadius, double impactSpeed,
                              Color color)
{
    int count = 5 + (int)ClampDouble(mergedRadius * 0.8, 0.0, 19.0);

    for (int i = 0; i < count; i++) {
        double angle = RandomDouble(0.0, TAU);
        double speed = RandomDouble(18.0, 70.0)
                     + ClampDouble(impactSpeed * 0.30, 0.0, 80.0);
        Vec2d burst = V2d(cos(angle) * speed, sin(angle) * speed);
        Vec2d velocity = V2dAdd(V2dScale(baseVelocity, 0.25), burst);

        SpawnParticle(
            pos,
            velocity,
            (float)RandomDouble(0.45, 1.35),
            (float)RandomDouble(0.7, 2.5 + mergedRadius * 0.06),
            color
        );
    }

    if (mergedRadius >= 7.0 || impactSpeed > 55.0) {
        SpawnShockwave(
            pos,
            (float)(55.0 + ClampDouble(impactSpeed, 0.0, 100.0)),
            0.8f,
            color
        );
    }
}

static void MergeBodies(Body *a, Body *b)
{
    double totalMass = a->mass + b->mass;
    if (totalMass <= 0.0) return;

    Vec2d relativeVelocity = V2dSub(a->vel, b->vel);
    double impactSpeed = V2dLength(relativeVelocity);

    Vec2d newPos = V2dScale(
        V2dAdd(V2dScale(a->pos, a->mass),
               V2dScale(b->pos, b->mass)),
        1.0 / totalMass
    );

    Vec2d newVelocity = V2dScale(
        V2dAdd(V2dScale(a->vel, a->mass),
               V2dScale(b->vel, b->mass)),
        1.0 / totalMass
    );

    Color newColor = MixColors(a->color, b->color, b->mass / totalMass);
    double newRadius = sqrt(a->radius * a->radius
                          + b->radius * b->radius);

    a->pos = newPos;
    a->vel = newVelocity;
    a->mass = totalMass;
    a->radius = newRadius;
    a->color = newColor;
    a->acc = V2d(0.0, 0.0);

    b->active = false;
    ClearTrail(a);
    PushTrailPoint(a);

    SpawnMergeEffects(newPos, newVelocity, newRadius, impactSpeed, newColor);
}

static void UpdateEffects(double dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *particle = &gParticles[i];
        if (!particle->active) continue;

        particle->life -= (float)dt;
        if (particle->life <= 0.0f) {
            particle->active = false;
            continue;
        }

        particle->pos = V2dAdd(particle->pos, V2dScale(particle->vel, dt));
        particle->vel = V2dScale(particle->vel, pow(0.25, dt));
    }

    for (int i = 0; i < MAX_SHOCKWAVES; i++) {
        Shockwave *shockwave = &gShockwaves[i];
        if (!shockwave->active) continue;

        shockwave->life -= (float)dt;
        if (shockwave->life <= 0.0f) {
            shockwave->active = false;
            continue;
        }

        shockwave->radius += shockwave->speed * (float)dt;
        shockwave->speed *= (float)pow(0.35, dt);
    }
}

static int CountActiveBodies(void)
{
    int count = 0;
    for (int i = 0; i < gBodySlots; i++) {
        if (gBodies[i].active) count++;
    }
    return count;
}

static Body *FindMostMassiveBody(void)
{
    Body *result = NULL;

    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active) continue;

        if (result == NULL || body->mass > result->mass) {
            result = body;
        }
    }

    return result;
}

static Vec2d GetCenterOfMass(void)
{
    Vec2d weightedPosition = V2d(0.0, 0.0);
    double totalMass = 0.0;

    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active) continue;

        weightedPosition = V2dAdd(
            weightedPosition,
            V2dScale(body->pos, body->mass)
        );
        totalMass += body->mass;
    }

    if (totalMass <= 0.0) return V2d(0.0, 0.0);
    return V2dScale(weightedPosition, 1.0 / totalMass);
}

/* -------------------------------------------------------------------------- */
/* Physics                                                                     */
/* -------------------------------------------------------------------------- */

static void PhysicsStep(double dt)
{
    const double baseSoftening = 3.5;
    const double maxAcceleration = 2800.0;

    for (int i = 0; i < gBodySlots; i++) {
        if (gBodies[i].active) {
            gBodies[i].acc = V2d(0.0, 0.0);
        }
    }

    /*
        Each pair is visited exactly once. The equal and opposite acceleration
        updates preserve momentum much better than updating bodies separately.
    */
    for (int i = 0; i < gBodySlots; i++) {
        Body *a = &gBodies[i];
        if (!a->active) continue;

        for (int j = i + 1; j < gBodySlots; j++) {
            Body *b = &gBodies[j];
            if (!b->active) continue;

            Vec2d delta = V2dSub(b->pos, a->pos);
            double distanceSquared = delta.x * delta.x + delta.y * delta.y;
            double collisionRadius = a->radius + b->radius;

            if (gState.collisions
                && distanceSquared <= collisionRadius * collisionRadius) {
                MergeBodies(a, b);
                continue;
            }

            double softening = baseSoftening
                             + 0.08 * (a->radius + b->radius);
            double softenedSquared = distanceSquared
                                   + softening * softening;
            double inverseDistance = 1.0 / sqrt(softenedSquared);
            double inverseDistanceCubed = inverseDistance
                                         * inverseDistance
                                         * inverseDistance;
            double gravityFactor = gState.gravity * inverseDistanceCubed;

            a->acc.x += delta.x * gravityFactor * b->mass;
            a->acc.y += delta.y * gravityFactor * b->mass;
            b->acc.x -= delta.x * gravityFactor * a->mass;
            b->acc.y -= delta.y * gravityFactor * a->mass;
        }
    }

    /*
        Semi-implicit Euler: velocity is updated before position. It is simple,
        fast, and much more stable for orbital motion than explicit Euler.
    */
    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active) continue;

        double acceleration = V2dLength(body->acc);
        if (acceleration > maxAcceleration) {
            body->acc = V2dScale(body->acc, maxAcceleration / acceleration);
        }

        body->vel = V2dAdd(body->vel, V2dScale(body->acc, dt));
        body->pos = V2dAdd(body->pos, V2dScale(body->vel, dt));
    }

    gState.trailClock += dt;
    if (gState.trailClock >= TRAIL_INTERVAL) {
        gState.trailClock = fmod(gState.trailClock, TRAIL_INTERVAL);

        for (int i = 0; i < gBodySlots; i++) {
            if (gBodies[i].active) PushTrailPoint(&gBodies[i]);
        }
    }

    UpdateEffects(dt);
}

/* -------------------------------------------------------------------------- */
/* Presets                                                                     */
/* -------------------------------------------------------------------------- */

static void ClearWorld(void)
{
    memset(gBodies, 0, sizeof(gBodies));
    memset(gParticles, 0, sizeof(gParticles));
    memset(gShockwaves, 0, sizeof(gShockwaves));

    gBodySlots = 0;
    gParticleCursor = 0;
    gShockwaveCursor = 0;
    gState.trailClock = 0.0;
}

static void GenerateGalaxy(Vec2d center, Vec2d baseVelocity,
                           int particleCount, double scale,
                           double tilt, int spin, Color coreColor)
{
    const double flattening = 0.68;
    double coreMass = 18000.0 * scale * scale;
    double coreRadius = 18.0 * sqrt(scale);

    AddBody(center, baseVelocity, coreRadius, coreMass, coreColor);

    for (int i = 0; i < particleCount; i++) {
        double angle = RandomDouble(0.0, TAU);
        double radius = (42.0 + pow(RandomDouble(0.0, 1.0), 0.58) * 430.0)
                      * scale;

        Vec2d localPosition = V2d(
            cos(angle) * radius,
            sin(angle) * radius * flattening
        );
        localPosition = V2dRotate(localPosition, tilt);

        Vec2d tangent = V2d(
            -sin(angle) * spin,
             cos(angle) * flattening * spin
        );
        tangent = V2dNormalize(V2dRotate(tangent, tilt));

        double speed = sqrt(gState.gravity * coreMass / (radius + 24.0))
                     * RandomDouble(0.91, 1.08);

        Vec2d velocity = V2dAdd(
            baseVelocity,
            V2dAdd(
                V2dScale(tangent, speed),
                V2d(RandomDouble(-2.0, 2.0), RandomDouble(-2.0, 2.0))
            )
        );

        double bodyRadius = RandomDouble(1.1, 3.25);
        double mass = RandomDouble(3.0, 18.0) * bodyRadius;

        int colorIndex = (int)(
            (angle / TAU) * (PALETTE_COUNT - 1)
            + RandomDouble(0.0, 2.0)
        ) % PALETTE_COUNT;

        AddBody(
            V2dAdd(center, localPosition),
            velocity,
            bodyRadius,
            mass,
            PALETTE[colorIndex]
        );
    }
}

static void PresetSpiralGalaxy(Camera2D *camera)
{
    GenerateGalaxy(
        V2d(0.0, 0.0),
        V2d(0.0, 0.0),
        520,
        1.0,
        -0.16,
        1,
        (Color){ 255, 244, 196, 255 }
    );

    camera->target = (Vector2){ 0.0f, 0.0f };
    camera->zoom = 1.02f;
}

static void PresetBinaryStars(Camera2D *camera)
{
    const double starMass = 8500.0;
    const double orbitRadius = 120.0;
    const double speed = sqrt(
        gState.gravity * starMass / (4.0 * orbitRadius)
    );

    AddBody(
        V2d(-orbitRadius, 0.0),
        V2d(0.0, -speed),
        18.0,
        starMass,
        (Color){ 255, 200, 104, 255 }
    );

    AddBody(
        V2d(orbitRadius, 0.0),
        V2d(0.0, speed),
        18.0,
        starMass,
        (Color){ 125, 211, 252, 255 }
    );

    for (int i = 0; i < 390; i++) {
        double angle = RandomDouble(0.0, TAU);
        double radius = RandomDouble(285.0, 680.0);
        double speedAroundPair = sqrt(
            gState.gravity * starMass * 2.0 / radius
        ) * RandomDouble(0.93, 1.06);

        Vec2d pos = V2d(
            cos(angle) * radius,
            sin(angle) * radius * 0.82
        );
        Vec2d tangent = V2dNormalize(
            V2d(-sin(angle), cos(angle) * 0.82)
        );

        double bodyRadius = RandomDouble(1.0, 3.1);
        AddBody(
            pos,
            V2dAdd(
                V2dScale(tangent, speedAroundPair),
                V2d(RandomDouble(-1.5, 1.5), RandomDouble(-1.5, 1.5))
            ),
            bodyRadius,
            RandomDouble(3.0, 17.0) * bodyRadius,
            PALETTE[(i / 24 + rand() % 2) % PALETTE_COUNT]
        );
    }

    camera->target = (Vector2){ 0.0f, 0.0f };
    camera->zoom = 0.82f;
}

static void AddMoon(Body *planet, double distance, double radius,
                    double mass, Color color)
{
    if (planet == NULL) return;

    double moonSpeed = sqrt(gState.gravity * planet->mass / distance);
    Vec2d moonPos = V2dAdd(planet->pos, V2d(distance, 0.0));
    Vec2d moonVel = V2dAdd(planet->vel, V2d(0.0, moonSpeed));

    AddBody(moonPos, moonVel, radius, mass, color);
}

static void PresetSolarSystem(Camera2D *camera)
{
    typedef struct {
        double distance;
        double radius;
        double mass;
        Color color;
    } PlanetDescription;

    const double sunMass = 30000.0;
    const PlanetDescription planets[] = {
        {  82.0,  3.2,   55.0, { 190, 184, 178, 255 } },
        { 125.0,  4.8,  110.0, { 251, 166,  92, 255 } },
        { 180.0,  6.1,  240.0, {  96, 165, 250, 255 } },
        { 238.0,  5.2,  170.0, { 248, 113, 113, 255 } },
        { 350.0, 13.5, 2300.0, { 251, 191,  36, 255 } },
        { 470.0, 11.4, 1700.0, { 253, 230, 138, 255 } },
        { 575.0,  9.1, 1050.0, { 103, 232, 249, 255 } },
        { 670.0,  8.6,  920.0, {  59, 130, 246, 255 } }
    };

    AddBody(
        V2d(0.0, 0.0),
        V2d(0.0, 0.0),
        24.0,
        sunMass,
        (Color){ 255, 228, 128, 255 }
    );

    Body *earth = NULL;
    Body *jupiter = NULL;

    for (int i = 0; i < (int)(sizeof(planets) / sizeof(planets[0])); i++) {
        double angle = RandomDouble(0.0, TAU);
        double speed = sqrt(
            gState.gravity * sunMass / planets[i].distance
        );

        Vec2d pos = V2d(
            cos(angle) * planets[i].distance,
            sin(angle) * planets[i].distance
        );
        Vec2d vel = V2d(-sin(angle) * speed, cos(angle) * speed);

        Body *planet = AddBody(
            pos,
            vel,
            planets[i].radius,
            planets[i].mass,
            planets[i].color
        );

        if (i == 2) earth = planet;
        if (i == 4) jupiter = planet;
    }

    AddMoon(earth, 17.0, 1.8, 3.0, (Color){ 226, 232, 240, 255 });
    AddMoon(jupiter, 22.0, 2.1, 4.0, (Color){ 253, 224,  71, 255 });

    for (int i = 0; i < 190; i++) {
        double angle = RandomDouble(0.0, TAU);
        double radius = RandomDouble(276.0, 315.0);
        double speed = sqrt(gState.gravity * sunMass / radius)
                     * RandomDouble(0.96, 1.04);

        AddBody(
            V2d(cos(angle) * radius, sin(angle) * radius),
            V2d(-sin(angle) * speed, cos(angle) * speed),
            RandomDouble(0.55, 1.35),
            RandomDouble(0.5, 2.5),
            (Color){ 148, 163, 184, 255 }
        );
    }

    camera->target = (Vector2){ 0.0f, 0.0f };
    camera->zoom = 0.80f;
}

static void PresetGalaxyCollision(Camera2D *camera)
{
    GenerateGalaxy(
        V2d(-430.0, -120.0),
        V2d(24.0, 5.5),
        230,
        0.72,
        0.13,
        1,
        (Color){ 255, 190, 120, 255 }
    );

    GenerateGalaxy(
        V2d(430.0, 120.0),
        V2d(-24.0, -5.5),
        230,
        0.72,
        -0.23,
        -1,
        (Color){ 147, 197, 253, 255 }
    );

    camera->target = (Vector2){ 0.0f, 0.0f };
    camera->zoom = 0.82f;
}

static void PresetFigureEight(Camera2D *camera)
{
    /*
        The famous equal-mass three-body figure-eight orbit. These normalized
        initial conditions are scaled to this simulation's units.
    */
    const double lengthScale = 170.0;
    const double mass = 6000.0;
    const double velocityScale = sqrt(
        gState.gravity * mass / lengthScale
    );

    Vec2d p1 = V2d(-0.97000436 * lengthScale,
                    0.24308753 * lengthScale);
    Vec2d p2 = V2d( 0.97000436 * lengthScale,
                   -0.24308753 * lengthScale);
    Vec2d p3 = V2d(0.0, 0.0);

    Vec2d v1 = V2d(0.4662036850 * velocityScale,
                   0.4323657300 * velocityScale);
    Vec2d v2 = v1;
    Vec2d v3 = V2d(-0.9324073700 * velocityScale,
                   -0.8647314600 * velocityScale);

    AddBody(p1, v1, 13.0, mass, (Color){ 251, 113, 133, 255 });
    AddBody(p2, v2, 13.0, mass, (Color){  96, 165, 250, 255 });
    AddBody(p3, v3, 13.0, mass, (Color){ 253, 224,  71, 255 });

    camera->target = (Vector2){ 0.0f, 0.0f };
    camera->zoom = 1.75f;
}

static const char *GetPresetName(int preset)
{
    switch (preset) {
        case 1: return "Spiral Galaxy";
        case 2: return "Binary Stars";
        case 3: return "Solar System";
        case 4: return "Galaxy Collision";
        case 5: return "Figure-Eight";
        default: return "Unknown";
    }
}

static void LoadPreset(int preset, Camera2D *camera)
{
    ClearWorld();

    gState.preset = preset;
    gState.paused = false;
    gState.followMassive = false;
    gState.launching = false;
    gState.timeScale = 1.0;

    switch (preset) {
        case 1: PresetSpiralGalaxy(camera); break;
        case 2: PresetBinaryStars(camera); break;
        case 3: PresetSolarSystem(camera); break;
        case 4: PresetGalaxyCollision(camera); break;
        case 5: PresetFigureEight(camera); break;
        default: PresetSpiralGalaxy(camera); break;
    }
}

/* -------------------------------------------------------------------------- */
/* Background and world rendering                                             */
/* -------------------------------------------------------------------------- */

static void InitializeBackgroundStars(void)
{
    for (int i = 0; i < BACKGROUND_STARS; i++) {
        gStars[i].x = (float)RandomDouble(0.0, 1.0);
        gStars[i].y = (float)RandomDouble(0.0, 1.0);
        gStars[i].size = (float)RandomDouble(0.45, 2.1);
        gStars[i].depth = (float)RandomDouble(0.25, 1.0);
        gStars[i].phase = (float)RandomDouble(0.0, TAU);

        if (i % 13 == 0) {
            gStars[i].color = (Color){ 165, 180, 252, 255 };
        } else if (i % 17 == 0) {
            gStars[i].color = (Color){ 253, 224, 171, 255 };
        } else {
            gStars[i].color = (Color){ 226, 232, 240, 255 };
        }
    }
}

static float WrapFloat(float value, float maximum)
{
    value = fmodf(value, maximum);
    if (value < 0.0f) value += maximum;
    return value;
}

static void DrawBackground(const Camera2D *camera)
{
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    double time = GetTime();

    DrawRectangleGradientV(
        0,
        0,
        width,
        height,
        (Color){ 3, 6, 18, 255 },
        (Color){ 9, 5, 24, 255 }
    );

    /* Very faint color clouds make the empty areas feel less flat. */
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient(
        width / 5,
        height / 4,
        (float)(height * 0.42),
        (Color){ 20, 34, 92, 24 },
        (Color){ 0, 0, 0, 0 }
    );
    DrawCircleGradient(
        width * 4 / 5,
        height * 3 / 4,
        (float)(height * 0.38),
        (Color){ 78, 20, 92, 18 },
        (Color){ 0, 0, 0, 0 }
    );
    EndBlendMode();

    for (int i = 0; i < BACKGROUND_STARS; i++) {
        BackgroundStar *star = &gStars[i];

        float x = star->x * width
                - camera->target.x * 0.018f * star->depth;
        float y = star->y * height
                - camera->target.y * 0.018f * star->depth;
        x = WrapFloat(x, (float)width);
        y = WrapFloat(y, (float)height);

        float twinkle = 0.58f
                      + 0.42f * sinf((float)time * (0.7f + star->depth)
                                   + star->phase);
        float size = star->size * (0.75f + star->depth * 0.45f);

        DrawCircleV(
            (Vector2){ x, y },
            size,
            Fade(star->color, twinkle * 0.82f)
        );
    }
}

static void DrawGravityField(const Camera2D *camera)
{
    const int spacing = 72;
    int width = GetScreenWidth();
    int height = GetScreenHeight();

    for (int sy = spacing / 2; sy < height; sy += spacing) {
        for (int sx = spacing / 2; sx < width; sx += spacing) {
            Vector2 screenPoint = { (float)sx, (float)sy };
            Vector2 worldPointF = GetScreenToWorld2D(screenPoint, *camera);
            Vec2d worldPoint = FromVector2(worldPointF);
            Vec2d acceleration = V2d(0.0, 0.0);

            for (int i = 0; i < gBodySlots; i++) {
                Body *body = &gBodies[i];
                if (!body->active) continue;

                Vec2d delta = V2dSub(body->pos, worldPoint);
                double distanceSquared = delta.x * delta.x
                                       + delta.y * delta.y
                                       + 20.0;
                double inverseDistance = 1.0 / sqrt(distanceSquared);
                double factor = gState.gravity * body->mass
                              * inverseDistance
                              * inverseDistance
                              * inverseDistance;

                acceleration.x += delta.x * factor;
                acceleration.y += delta.y * factor;
            }

            double magnitude = V2dLength(acceleration);
            if (magnitude < 0.0001) continue;

            Vec2d direction = V2dScale(acceleration, 1.0 / magnitude);
            float length = ClampFloat(
                3.0f + (float)log10(magnitude + 1.0) * 6.0f,
                4.0f,
                24.0f
            );

            Vector2 end = {
                screenPoint.x + (float)direction.x * length,
                screenPoint.y + (float)direction.y * length
            };

            DrawLineEx(
                screenPoint,
                end,
                1.15f,
                (Color){ 129, 140, 248, 72 }
            );

            Vector2 side = {
                (float)-direction.y * 2.7f,
                (float) direction.x * 2.7f
            };
            Vector2 back = {
                end.x - (float)direction.x * 5.0f,
                end.y - (float)direction.y * 5.0f
            };
            DrawTriangle(
                end,
                (Vector2){ back.x + side.x, back.y + side.y },
                (Vector2){ back.x - side.x, back.y - side.y },
                (Color){ 165, 180, 252, 72 }
            );
        }
    }
}

static void DrawTrails(const Camera2D *camera)
{
    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active || body->trailCount < 2) continue;

        int oldest = (body->trailHead - body->trailCount + TRAIL_POINTS)
                   % TRAIL_POINTS;

        for (int point = 1; point < body->trailCount; point++) {
            int previousIndex = (oldest + point - 1) % TRAIL_POINTS;
            int currentIndex = (oldest + point) % TRAIL_POINTS;
            float age = (float)point / (float)(body->trailCount - 1);
            float alpha = age * age * 0.50f;
            float width = (float)fmax(
                0.55 / camera->zoom,
                body->radius * (0.08 + age * 0.16)
            );

            DrawLineEx(
                ToVector2(body->trail[previousIndex]),
                ToVector2(body->trail[currentIndex]),
                width,
                Fade(body->color, alpha)
            );
        }
    }
}

static void DrawEffects(const Camera2D *camera)
{
    for (int i = 0; i < MAX_SHOCKWAVES; i++) {
        Shockwave *shockwave = &gShockwaves[i];
        if (!shockwave->active) continue;

        float life = shockwave->life / shockwave->maxLife;
        float thickness = (1.5f + life * 2.0f) / camera->zoom;

        DrawRing(
            ToVector2(shockwave->pos),
            fmaxf(0.0f, shockwave->radius - thickness),
            shockwave->radius + thickness,
            0.0f,
            360.0f,
            64,
            Fade(shockwave->color, life * 0.55f)
        );
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *particle = &gParticles[i];
        if (!particle->active) continue;

        float life = particle->life / particle->maxLife;
        float size = particle->size * (0.4f + life * 0.8f);

        DrawCircleV(
            ToVector2(particle->pos),
            size * 2.8f,
            Fade(particle->color, life * 0.10f)
        );
        DrawCircleV(
            ToVector2(particle->pos),
            size,
            Fade(particle->color, life * 0.85f)
        );
    }
}

static void DrawBodyGlows(void)
{
    double time = GetTime();

    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active) continue;

        Vector2 pos = ToVector2(body->pos);
        float radius = (float)body->radius;
        float pulse = 1.0f + 0.035f * sinf(
            (float)time * 2.2f + (float)i * 0.73f
        );

        DrawCircleV(pos, radius * 3.8f * pulse, Fade(body->color, 0.045f));
        DrawCircleV(pos, radius * 2.45f * pulse, Fade(body->color, 0.085f));
        DrawCircleV(pos, radius * 1.55f * pulse, Fade(body->color, 0.16f));

        if (body->mass > 2500.0) {
            float rotation = fmodf(
                (float)time * 9.0f + (float)i * 37.0f,
                360.0f
            );
            DrawRing(
                pos,
                radius * 1.45f,
                radius * 1.62f,
                rotation,
                rotation + 300.0f,
                72,
                Fade(body->color, 0.26f)
            );
        }
    }
}

static void DrawBodyCores(void)
{
    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active) continue;

        Vector2 pos = ToVector2(body->pos);
        float radius = (float)body->radius;

        DrawCircleV(pos, radius, body->color);
        DrawCircleV(
            (Vector2){
                pos.x - radius * 0.20f,
                pos.y - radius * 0.20f
            },
            fmaxf(0.55f, radius * 0.32f),
            Fade(WHITE, 0.72f)
        );
    }
}

static void DrawArrowWorld(Vec2d start, Vec2d velocity,
                           const Camera2D *camera, Color color)
{
    double speed = V2dLength(velocity);
    if (speed < 0.01) return;

    double worldLength = ClampDouble(
        speed * 0.50,
        10.0 / camera->zoom,
        70.0 / camera->zoom
    );
    Vec2d direction = V2dNormalize(velocity);
    Vec2d end = V2dAdd(start, V2dScale(direction, worldLength));
    Vec2d side = V2d(-direction.y, direction.x);
    double headSize = 5.0 / camera->zoom;
    Vec2d headBase = V2dSub(end, V2dScale(direction, headSize * 1.7));

    DrawLineEx(
        ToVector2(start),
        ToVector2(end),
        1.35f / camera->zoom,
        color
    );
    DrawTriangle(
        ToVector2(end),
        ToVector2(V2dAdd(headBase, V2dScale(side, headSize))),
        ToVector2(V2dSub(headBase, V2dScale(side, headSize))),
        color
    );
}

static void DrawVelocityVectors(const Camera2D *camera)
{
    for (int i = 0; i < gBodySlots; i++) {
        Body *body = &gBodies[i];
        if (!body->active) continue;
        DrawArrowWorld(
            body->pos,
            body->vel,
            camera,
            Fade((Color){ 134, 239, 172, 255 }, 0.62f)
        );
    }
}

static void DrawWorld(const Camera2D *camera)
{
    BeginBlendMode(BLEND_ADDITIVE);
    if (gState.showTrails) DrawTrails(camera);
    DrawEffects(camera);
    DrawBodyGlows();
    EndBlendMode();

    DrawBodyCores();

    if (gState.showVelocity) {
        DrawVelocityVectors(camera);
    }
}

/* -------------------------------------------------------------------------- */
/* Interface                                                                   */
/* -------------------------------------------------------------------------- */

static void DrawLaunchPreview(const Camera2D *camera)
{
    if (!gState.launching) return;

    Vector2 start = GetWorldToScreen2D(ToVector2(gState.launchStart), *camera);
    Vector2 end = GetMousePosition();
    Vector2 delta = { end.x - start.x, end.y - start.y };
    float length = sqrtf(delta.x * delta.x + delta.y * delta.y);
    bool massive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    Color color = massive
                ? (Color){ 253, 224, 71, 255 }
                : (Color){ 125, 211, 252, 255 };

    DrawCircleLines(
        (int)start.x,
        (int)start.y,
        massive ? 12.0f * camera->zoom : 4.5f * camera->zoom,
        Fade(color, 0.9f)
    );

    if (length > 1.0f) {
        Vector2 direction = { delta.x / length, delta.y / length };
        Vector2 side = { -direction.y * 6.0f, direction.x * 6.0f };
        Vector2 headBase = {
            end.x - direction.x * 14.0f,
            end.y - direction.y * 14.0f
        };

        DrawLineEx(start, end, 2.0f, Fade(color, 0.85f));
        DrawTriangle(
            end,
            (Vector2){ headBase.x + side.x, headBase.y + side.y },
            (Vector2){ headBase.x - side.x, headBase.y - side.y },
            color
        );
    }

    Vec2d currentWorld = FromVector2(
        GetScreenToWorld2D(end, *camera)
    );
    double launchSpeed = V2dLength(
        V2dScale(V2dSub(currentWorld, gState.launchStart), 0.65)
    );

    DrawText(
        TextFormat("%s body  |  launch speed %.1f",
                   massive ? "Massive" : "Normal",
                   launchSpeed),
        (int)end.x + 15,
        (int)end.y + 13,
        18,
        Fade(RAYWHITE, 0.88f)
    );
}

static void DrawStatusPill(int x, int y, const char *label,
                           const char *value, Color accent)
{
    int labelWidth = MeasureText(label, 16);
    int valueWidth = MeasureText(value, 18);
    int width = labelWidth + valueWidth + 34;

    DrawRectangleRounded(
        (Rectangle){ (float)x, (float)y, (float)width, 30.0f },
        0.45f,
        8,
        (Color){ 15, 23, 42, 210 }
    );
    DrawCircle(x + 13, y + 15, 3.0f, accent);
    DrawText(label, x + 22, y + 7, 16, (Color){ 148, 163, 184, 255 });
    DrawText(value, x + 27 + labelWidth, y + 6, 18, RAYWHITE);
}

static void DrawHud(const Camera2D *camera)
{
    (void)camera;

    int width = GetScreenWidth();
    int activeBodies = CountActiveBodies();
    int panelWidth = 288;

    DrawRectangleRounded(
        (Rectangle){ 18.0f, 18.0f, (float)panelWidth, 145.0f },
        0.09f,
        8,
        (Color){ 6, 10, 24, 220 }
    );
    DrawRectangle(18, 18, 4, 145, (Color){ 99, 102, 241, 255 });

    DrawText("N-BODY", 38, 32, 30, RAYWHITE);
    DrawText("COSMIC SANDBOX", 161, 41, 14,
             (Color){ 165, 180, 252, 255 });

    DrawText(
        TextFormat("%d  %s", gState.preset, GetPresetName(gState.preset)),
        38,
        72,
        20,
        (Color){ 203, 213, 225, 255 }
    );
    DrawText(
        TextFormat("Bodies  %d", activeBodies),
        38,
        103,
        18,
        (Color){ 148, 163, 184, 255 }
    );
    DrawText(
        TextFormat("Gravity %.1f   Zoom %.2fx",
                   gState.gravity,
                   camera->zoom),
        38,
        130,
        16,
        (Color){ 100, 116, 139, 255 }
    );

    DrawStatusPill(
        width - 263,
        20,
        "TIME",
        TextFormat("%.2fx", gState.timeScale),
        (Color){ 134, 239, 172, 255 }
    );
    DrawStatusPill(
        width - 132,
        20,
        "FPS",
        TextFormat("%d", GetFPS()),
        (Color){ 125, 211, 252, 255 }
    );

    if (gState.paused) {
        const char *pausedText = "PAUSED";
        int textWidth = MeasureText(pausedText, 20);
        int x = width / 2 - textWidth / 2 - 19;

        DrawRectangleRounded(
            (Rectangle){ (float)x, 20.0f,
                         (float)(textWidth + 38), 34.0f },
            0.45f,
            8,
            (Color){ 127, 29, 29, 225 }
        );
        DrawText(pausedText, x + 19, 27, 20,
                 (Color){ 254, 202, 202, 255 });
    }

    DrawText(
        "LMB drag: launch    RMB: pan    Wheel: zoom    H: help",
        20,
        GetScreenHeight() - 30,
        17,
        (Color){ 148, 163, 184, 220 }
    );

    if (gState.followMassive) {
        const char *text = "FOLLOWING MOST MASSIVE BODY";
        int textWidth = MeasureText(text, 15);
        DrawText(
            text,
            width - textWidth - 20,
            GetScreenHeight() - 29,
            15,
            (Color){ 253, 224, 71, 220 }
        );
    }
}

static void DrawHelpPanel(void)
{
    if (!gState.showHelp) return;

    const int panelWidth = 650;
    const int panelHeight = 560;
    int x = GetScreenWidth() / 2 - panelWidth / 2;
    int y = GetScreenHeight() / 2 - panelHeight / 2;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 145 });
    DrawRectangleRounded(
        (Rectangle){ (float)x, (float)y,
                     (float)panelWidth, (float)panelHeight },
        0.035f,
        12,
        (Color){ 8, 13, 29, 246 }
    );
    DrawRectangle(x, y, 5, panelHeight, (Color){ 99, 102, 241, 255 });

    DrawText("CONTROLS", x + 34, y + 27, 31, RAYWHITE);
    DrawText(
        "Everything here is interactive - disturb the presets.",
        x + 34,
        y + 67,
        17,
        (Color){ 148, 163, 184, 255 }
    );

    const char *leftKeys[] = {
        "1 - 5", "R", "Space", "N", "Left drag", "Shift + drag",
        "Right drag", "Mouse wheel", "C", "F"
    };
    const char *leftActions[] = {
        "Load a space preset", "Restart current preset", "Pause / resume",
        "Single step while paused", "Launch a new body",
        "Launch a massive body", "Pan the camera",
        "Zoom toward cursor", "Center on center of mass",
        "Follow the most massive body"
    };
    const char *rightKeys[] = {
        "T", "V", "B", "M", "- / =", "[ / ]", "P", "F11", "H", "Esc"
    };
    const char *rightActions[] = {
        "Toggle trails", "Toggle gravity field", "Toggle velocity vectors",
        "Toggle collisions", "Change time speed", "Change gravity",
        "Save screenshot", "Toggle fullscreen", "Close this panel",
        "Exit"
    };

    int startY = y + 112;
    for (int i = 0; i < 10; i++) {
        int rowY = startY + i * 38;

        DrawText(leftKeys[i], x + 34, rowY, 18,
                 (Color){ 165, 180, 252, 255 });
        DrawText(leftActions[i], x + 145, rowY, 17,
                 (Color){ 226, 232, 240, 255 });

        DrawText(rightKeys[i], x + 361, rowY, 18,
                 (Color){ 125, 211, 252, 255 });
        DrawText(rightActions[i], x + 440, rowY, 17,
                 (Color){ 226, 232, 240, 255 });
    }

    DrawRectangleRounded(
        (Rectangle){ (float)(x + 31), (float)(y + panelHeight - 61),
                     (float)(panelWidth - 62), 35.0f },
        0.25f,
        8,
        (Color){ 30, 41, 59, 210 }
    );
    DrawText(
        TextFormat("Trails %s   Field %s   Vectors %s   Collisions %s",
                   gState.showTrails ? "ON" : "OFF",
                   gState.showField ? "ON" : "OFF",
                   gState.showVelocity ? "ON" : "OFF",
                   gState.collisions ? "ON" : "OFF"),
        x + 51,
        y + panelHeight - 52,
        17,
        (Color){ 203, 213, 225, 255 }
    );
}

/* -------------------------------------------------------------------------- */
/* Input and camera                                                            */
/* -------------------------------------------------------------------------- */

static void CenterCamera(Camera2D *camera)
{
    camera->target = ToVector2(GetCenterOfMass());
    gState.followMassive = false;
}

static void HandleKeyboard(Camera2D *camera)
{
    if (IsKeyPressed(KEY_ONE))   LoadPreset(1, camera);
    if (IsKeyPressed(KEY_TWO))   LoadPreset(2, camera);
    if (IsKeyPressed(KEY_THREE)) LoadPreset(3, camera);
    if (IsKeyPressed(KEY_FOUR))  LoadPreset(4, camera);
    if (IsKeyPressed(KEY_FIVE))  LoadPreset(5, camera);

    if (IsKeyPressed(KEY_R)) LoadPreset(gState.preset, camera);
    if (IsKeyPressed(KEY_SPACE)) gState.paused = !gState.paused;
    if (IsKeyPressed(KEY_T)) gState.showTrails = !gState.showTrails;
    if (IsKeyPressed(KEY_V)) gState.showField = !gState.showField;
    if (IsKeyPressed(KEY_B)) gState.showVelocity = !gState.showVelocity;
    if (IsKeyPressed(KEY_M)) gState.collisions = !gState.collisions;
    if (IsKeyPressed(KEY_H)) gState.showHelp = !gState.showHelp;
    if (IsKeyPressed(KEY_P)) gState.screenshotRequested = true;

    if (IsKeyPressed(KEY_C)) CenterCamera(camera);

    if (IsKeyPressed(KEY_F)) {
        gState.followMassive = !gState.followMassive;
    }

    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        gState.timeScale = ClampDouble(
            gState.timeScale * 0.5,
            0.125,
            4.0
        );
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
        gState.timeScale = ClampDouble(
            gState.timeScale * 2.0,
            0.125,
            4.0
        );
    }

    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
        gState.gravity = ClampDouble(gState.gravity / 1.25, 3.0, 180.0);
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
        gState.gravity = ClampDouble(gState.gravity * 1.25, 3.0, 180.0);
    }

    if (IsKeyPressed(KEY_N) && gState.paused) {
        PhysicsStep(FIXED_DT);
    }

    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }
}

static void HandleCamera(Camera2D *camera)
{
    Vector2 mouse = GetMousePosition();
    float wheel = GetMouseWheelMove();

    if (wheel != 0.0f) {
        Vector2 beforeZoom = GetScreenToWorld2D(mouse, *camera);
        camera->zoom *= powf(1.16f, wheel);
        camera->zoom = ClampFloat(camera->zoom, MIN_ZOOM, MAX_ZOOM);
        Vector2 afterZoom = GetScreenToWorld2D(mouse, *camera);

        camera->target.x += beforeZoom.x - afterZoom.x;
        camera->target.y += beforeZoom.y - afterZoom.y;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();
        camera->target.x -= delta.x / camera->zoom;
        camera->target.y -= delta.y / camera->zoom;
        gState.followMassive = false;
    }

    if (gState.followMassive) {
        Body *body = FindMostMassiveBody();
        if (body != NULL) {
            float smoothing = 1.0f
                            - expf(-6.0f * ClampFloat(GetFrameTime(),
                                                     0.0f, 0.05f));
            camera->target.x += ((float)body->pos.x - camera->target.x)
                              * smoothing;
            camera->target.y += ((float)body->pos.y - camera->target.y)
                              * smoothing;
        }
    }
}

static void HandleBodyLauncher(const Camera2D *camera)
{
    if (gState.showHelp) {
        gState.launching = false;
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        gState.launchStart = FromVector2(
            GetScreenToWorld2D(GetMousePosition(), *camera)
        );
        gState.launching = true;
    }

    if (gState.launching && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        Vec2d end = FromVector2(
            GetScreenToWorld2D(GetMousePosition(), *camera)
        );
        Vec2d velocity = V2dScale(
            V2dSub(end, gState.launchStart),
            0.65
        );

        double speed = V2dLength(velocity);
        if (speed > 450.0) {
            velocity = V2dScale(velocity, 450.0 / speed);
        }

        bool massive = IsKeyDown(KEY_LEFT_SHIFT)
                    || IsKeyDown(KEY_RIGHT_SHIFT);

        if (massive) {
            AddBody(
                gState.launchStart,
                velocity,
                12.0,
                2600.0,
                (Color){ 253, 224, 71, 255 }
            );
        } else {
            double radius = RandomDouble(3.2, 5.5);
            AddBody(
                gState.launchStart,
                velocity,
                radius,
                radius * radius * 3.2,
                RandomPaletteColor()
            );
        }

        gState.launching = false;
    }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                        */
/* -------------------------------------------------------------------------- */

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(INITIAL_WIDTH, INITIAL_HEIGHT, "N-Body Cosmic Sandbox");
    SetWindowMinSize(900, 600);
    SetTargetFPS(60);

    srand((unsigned int)time(NULL));
    InitializeBackgroundStars();

    gState.gravity = DEFAULT_GRAVITY;
    gState.timeScale = 1.0;
    gState.showTrails = true;
    gState.showField = false;
    gState.showVelocity = false;
    gState.collisions = true;
    gState.showHelp = false;

    Camera2D camera = { 0 };
    camera.offset = (Vector2){
        GetScreenWidth() * 0.5f,
        GetScreenHeight() * 0.5f
    };
    camera.target = (Vector2){ 0.0f, 0.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    LoadPreset(1, &camera);

    double accumulator = 0.0;

    while (!WindowShouldClose()) {
        camera.offset = (Vector2){
            GetScreenWidth() * 0.5f,
            GetScreenHeight() * 0.5f
        };

        HandleKeyboard(&camera);
        HandleCamera(&camera);
        HandleBodyLauncher(&camera);

        double frameTime = ClampDouble(GetFrameTime(), 0.0, 0.05);

        if (!gState.paused) {
            accumulator += frameTime * gState.timeScale;

            int steps = 0;
            while (accumulator >= FIXED_DT
                   && steps < MAX_PHYSICS_STEPS) {
                PhysicsStep(FIXED_DT);
                accumulator -= FIXED_DT;
                steps++;
            }

            if (steps == MAX_PHYSICS_STEPS
                && accumulator >= FIXED_DT) {
                accumulator = fmod(accumulator, FIXED_DT);
            }
        } else {
            accumulator = 0.0;
        }

        BeginDrawing();
        DrawBackground(&camera);

        if (gState.showField) {
            DrawGravityField(&camera);
        }

        BeginMode2D(camera);
        DrawWorld(&camera);
        EndMode2D();

        DrawLaunchPreview(&camera);
        DrawHud(&camera);
        DrawHelpPanel();
        EndDrawing();

        if (gState.screenshotRequested) {
            TakeScreenshot("nbody_screenshot.png");
            gState.screenshotRequested = false;
        }
    }

    CloseWindow();
    return 0;
}
