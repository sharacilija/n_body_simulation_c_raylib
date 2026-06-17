#include <raylib.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 800
#define N_COLORS 6
#define G (10.0)
#define SOFTNESS (10.0)
#define LAST_POSITIONS 10

typedef struct {
    double x, y;
} Vector2d;

typedef struct {
    Vector2d pos;
    Vector2d vel; 
    Vector2d acc; //acceleration
    Vector2d f; //force
    Vector2d lastpos[LAST_POSITIONS];
    double radius;
    double mass;
    Color color;
    uint8_t active; //0 or 1
    size_t framecounter;
} Body;

typedef struct {
    double minR; //minimum radius
    double maxR;
    double initVel;
    double dt;
    size_t activeBod; //number of active bodies
} Configuration;

double GetRandomDouble(double min, double max)
{
    return min + ((double)rand() / RAND_MAX) * (max-min);
}

Color GetBodyColor(Color colors[N_COLORS], size_t index)
{
    return colors[index];
}

void GenerateBody(Body *b, Color colors[N_COLORS], Configuration *cf)
{
    b->pos.x = GetRandomDouble(0, SCREEN_WIDTH);
    b->pos.y = GetRandomDouble(0, SCREEN_HEIGHT);
    b->vel.x = GetRandomDouble(-cf->initVel, cf->initVel);
    b->vel.y = GetRandomDouble(-cf->initVel, cf->initVel);

    b->acc.x = 0;
    b->acc.y = 0;
    b->f.x = 0;
    b->f.y = 0;

    b->radius = GetRandomDouble(cf->minR, cf->maxR);
    int randColor = GetRandomValue(0, N_COLORS-1);
    b->color = GetBodyColor(colors, randColor);
    b->mass = GetRandomDouble(b->radius*b->radius, b->radius*b->radius*100);

    b->active = 1;

    for (size_t i = 0; i < LAST_POSITIONS; i++)
    {
        b->lastpos[i].x = -1;
        b->lastpos[i].y = -1;
    }

    b->framecounter = 0;
}

void SetBodies(Body *b, size_t n, Color colors[N_COLORS], Configuration *cf)
{
    for (size_t i = 0; i < n; i++)
    {
        GenerateBody(&b[i], colors, cf);
    }
}

double Hypothenuse(Body *b1, Body *b2)
{
    double x = b1->pos.x - b2->pos.x;
    double y = b1->pos.y - b2->pos.y;
    return sqrt(x*x + y*y);
}

double CalcNewton(Body *b1, Body *b2)
{
    double distance = Hypothenuse(b1, b2);
    return G * b1->mass * b2->mass / (distance * distance + SOFTNESS * SOFTNESS);
}

void ResetForce(Body *b)
{
    b->f.x = 0;
    b->f.y = 0;
}

void UpdateForce(Body *b1, Body *b2)
{
    if (b1->active && b2->active)
    {
        double force = CalcNewton(b1, b2);
        double dx = b2->pos.x - b1->pos.x;
        double dy = b2->pos.y - b1->pos.y;

        double r = Hypothenuse(b1, b2);
        double fx = force * dx / r;
        double fy = force * dy / r;

        //Fa = -Fb
        b1->f.x += fx;
        b1->f.y += fy;

        b2->f.x -= fx;
        b2->f.y -= fy;
    }
}

void UpdateAcc(Body *b)
{
    if (b->active)
    {
        b->acc.x = b->f.x / b->mass;
        b->acc.y = b->f.y / b->mass;  
    }
}

void UpdateVel(Body *b, double dt)
{
    if (b->active)
    {
        b->vel.x += b->acc.x * dt;
        b->vel.y += b->acc.y * dt;
    }
}

void UpdatePos(Body *b, double dt)
{
    if (b->active)
    {
        b->pos.x += b->vel.x * dt;
        b->pos.y += b->vel.y * dt;
    }
}

void UpdateActiveBod(Body *b, size_t n, Configuration *cf)
{
    size_t num = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (b[i].active)
        {
            num++;
        }
    }
    cf->activeBod = num;
}

void UpdateLastPos(Body *b)
{
    if (!b->active) return;

    b->framecounter++;
    if (b->framecounter % 5 == 0)
    {
        for (size_t i = LAST_POSITIONS-1; i > 0; i--)
        {
            b->lastpos[i].x = b->lastpos[i-1].x;
            b->lastpos[i].y = b->lastpos[i-1].y;
        }

        b->lastpos[0].x = b->pos.x;
        b->lastpos[0].y = b->pos.y;
    }
    if (b->framecounter >= 1000) b->framecounter = 0;
}

int IsColliding(Body *b1, Body *b2)
{
    if (b1->active && b2->active)
    {
        double centerDist = Hypothenuse(b1, b2);
        if (centerDist <= (b1->radius + b2->radius)) return 1;
    }

    return 0;
}

void Merge(Body *b1, Body *b2)
{
    if (!b1->active || !b2->active) return;

    if (IsColliding(b1, b2))
    {
        uint8_t activity = b1->radius > b2->radius ? 1 : 0;
        b1->active = activity;
        b2->active = !activity;

        double newMass = b1->mass + b2->mass;

        if (b1->active)
        {
            b1->mass = newMass;
        }
        else 
        {
            b2->mass = newMass;
        }
    }
}

void RenderBodies(Body *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (b[i].active)
        {
            DrawCircle((float)b[i].pos.x, (float)b[i].pos.y, (float)b[i].radius, b[i].color);
        }
    }
}

void RenderTrails(Body *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (!b[i].active) continue;

        for (size_t j = 0; j < LAST_POSITIONS; j++)
        {
            float posx = b[i].lastpos[j].x;
            float posy = b[i].lastpos[j].y;

            if (posx != -1)
            {
                float resize = (LAST_POSITIONS - (j+1)) / 10.0;
                DrawCircle(posx, posy, b[i].radius * resize, b[i].color);
            }
        }
    }
}

int main(void)
{
    
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "N_BODY SIMULATION");

    srand(time(NULL));

    Color colors[N_COLORS] = {RED, BLUE, GREEN, VIOLET, YELLOW, ORANGE};

    SetTargetFPS(60);

    size_t n = 1000;
    Body *bodies = malloc(n * sizeof(Body));
    Configuration cfg = {.minR=1, .maxR=9, .initVel=15, .activeBod=n};

    SetBodies(bodies, n, colors, &cfg);

    while(!WindowShouldClose())
    {

        cfg.dt = GetFrameTime();

        for (size_t i = 0; i < n; i++)
        {
            ResetForce(&bodies[i]);
        }

        for (size_t i = 0; i < n; i++)
        {
             for (size_t j = i+1; j < n; j++)
             {
                UpdateForce(&bodies[i], &bodies[j]);
                Merge(&bodies[i], &bodies[j]);
             }
        }

        UpdateActiveBod(bodies, n, &cfg);

        if (cfg.activeBod <= 50)
        {
            for (size_t i = 0; i < n; i++)
            {
                UpdateLastPos(&bodies[i]);
            }
        }

        for (size_t i = 0; i < n; i++)
        {
            UpdateAcc(&bodies[i]);
            UpdateVel(&bodies[i], cfg.dt);
            UpdatePos(&bodies[i], cfg.dt);
        }

        BeginDrawing();
            ClearBackground(BLACK);

        RenderTrails(bodies, n);
        RenderBodies(bodies, n);
        DrawText(TextFormat("Active Bodies: %zu", cfg.activeBod), 10, 10, 30, WHITE);

        EndDrawing();
    }

    CloseWindow();

    free(bodies);
    return 0;
}
