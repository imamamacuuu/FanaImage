#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h> // For usleep
#include <omp.h>

#define WIDTH 200
#define HEIGHT 100
#define NUM_OBJECTS 5 // 1 sun + 4 planets
#define G 0.1         // Gravitational constant (adjusted for stability)
#define DT 0.02       // Reduced time step for accuracy
#define MAX_SPEED 10  // Increased to allow orbital motion

typedef struct
{
    double x, y;   // Position
    double vx, vy; // Velocity
    double mass;   // Mass
    char symbol;   // Symbol for rendering
} Planet;

Planet planets[NUM_OBJECTS];

// Initialize planets to simulate a solar system
void initialize_planets()
{
    srand(42);

    // Central "Sun" (massive planet)
    planets[0].x = WIDTH / 2.0;
    planets[0].y = HEIGHT / 2.0;
    planets[0].vx = 0.0;
    planets[0].vy = 0.0;
    planets[0].mass = 100;   // Dominant mass
    planets[0].symbol = 'S'; // Sun symbol

    // Initialize planets with orbital velocities
    for (int i = 1; i < NUM_OBJECTS; i++)
    {
        double angle = (rand() % 360) * M_PI / 180.0; // Random angle
        double distance = 15.0 + (i * 5.0);           // Distance from the Sun

        // Position
        planets[i].x = WIDTH / 2.0 + distance * cos(angle);
        planets[i].y = HEIGHT / 2.0 + distance * sin(angle);

        // Orbital velocity (v = sqrt(G*M/r))
        double orbital_speed = sqrt(G * planets[0].mass / distance);
        planets[i].vx = -orbital_speed * sin(angle); // Tangential velocity
        planets[i].vy = orbital_speed * cos(angle);

        // Small mass (negligible compared to the Sun)
        planets[i].mass = 0.1;
        planets[i].symbol = "AOMK"[i % 4]; // Unique symbols for planets
    }
}

// Move the cursor to a specific position in the terminal
void move_cursor(int x, int y)
{
    printf("\033[%d;%dH", y + 1, x + 1);
}

// Render the planets on a 100x100 grid without clearing the screen
void render_planets()
{
    static char prev_grid[HEIGHT][WIDTH] = {0};
    char grid[HEIGHT][WIDTH];

    // Initialize the grid with empty spaces
    for (int i = 0; i < HEIGHT; i++)
    {
        for (int j = 0; j < WIDTH; j++)
        {
            grid[i][j] = '`';
        }
    }

    // Place planets on the grid
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        int x = (int)round(planets[i].x);
        int y = (int)round(planets[i].y);
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
        {
            grid[y][x] = planets[i].symbol;
        }
    }

    // Update only changed cells
    for (int i = 0; i < HEIGHT; i++)
    {
        for (int j = 0; j < WIDTH; j++)
        {
            if (grid[i][j] != prev_grid[i][j])
            {
                move_cursor(j, i);
                putchar(grid[i][j]);
                prev_grid[i][j] = grid[i][j];
            }
        }
    }

    move_cursor(0, HEIGHT);
    fflush(stdout);
}

// Update positions and velocities based on gravity
void update_planets()
{
    Planet temp_planets[NUM_OBJECTS];

// Copy data ke buffer sementara (agar tidak ada race condition saat membaca)
#pragma omp parallel for
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        temp_planets[i] = planets[i];
    }

// Perhitungan gaya gravitasi dan update kecepatan
#pragma omp parallel for
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        double fx = 0, fy = 0;

        for (int j = 0; j < NUM_OBJECTS; j++)
        {
            if (i == j)
                continue;

            double dx = temp_planets[j].x - temp_planets[i].x;
            double dy = temp_planets[j].y - temp_planets[i].y;
            double dist_sq = dx * dx + dy * dy;
            double dist = sqrt(dist_sq);

            if (dist < 1.0)
                dist = 1.0; // Prevent division by zero

            double force = (G * temp_planets[i].mass * temp_planets[j].mass) / dist_sq;
            fx += force * dx / dist;
            fy += force * dy / dist;
        }

        // Update velocity
        temp_planets[i].vx += fx / temp_planets[i].mass * DT;
        temp_planets[i].vy += fy / temp_planets[i].mass * DT;

        // Limit velocity
        double speed = sqrt(temp_planets[i].vx * temp_planets[i].vx + temp_planets[i].vy * temp_planets[i].vy);
        if (speed > MAX_SPEED)
        {
            temp_planets[i].vx *= MAX_SPEED / speed;
            temp_planets[i].vy *= MAX_SPEED / speed;
        }
    }

// Update posisi setelah kecepatan diperbarui
#pragma omp parallel for
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        temp_planets[i].x += temp_planets[i].vx * DT;
        temp_planets[i].y += temp_planets[i].vy * DT;

        // Stop jika planet keluar dari layar (reset posisi)
        if (temp_planets[i].x < 0 || temp_planets[i].x >= WIDTH ||
            temp_planets[i].y < 0 || temp_planets[i].y >= HEIGHT)
        {
#pragma omp critical
            initialize_planets();
        }
    }

// Salin kembali data dari `temp_planets` ke `planets`
#pragma omp parallel for
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        planets[i] = temp_planets[i];
    }
}

int main()
{
    // omp_set_num_threads(8);
    initialize_planets();
    printf("\033[?25l"); // Hide cursor

    while (1)
    {
        render_planets();
        update_planets();
        usleep(200); // delay for smooth animation
    }

    printf("\033[?25h"); // Restore cursor
    return 0;
}