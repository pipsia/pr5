#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ATTEMPTS 100

volatile sig_atomic_t guessed_number = 0;
volatile sig_atomic_t attempts = 0;
volatile sig_atomic_t game_over = 0;
volatile sig_atomic_t current_guess = 0;
volatile sig_atomic_t is_guesser = 0;

// Добавляем переменные для бинарного поиска
volatile sig_atomic_t min_range = 1;
volatile sig_atomic_t max_range = 100;

void guesser_signal_handler(int sig, siginfo_t *info, void *context) {
    if (sig == SIGUSR1) {
        game_over = 1;
        printf("Guesser (PID %d): Correct! Number %d guessed in %d attempts.\n", getpid(), current_guess, attempts);
        fflush(stdout);
    } else if (sig == SIGUSR2) {
        printf("Guesser (PID %d): Attempt %d: %d is wrong\n", getpid(), attempts, current_guess);
        fflush(stdout);
        
        // Обновляем границы поиска на основе ответа
        int response = info->si_value.sival_int; // 1 - больше, 0 - меньше
        if (response) {
            min_range = current_guess + 1;
        } else {
            max_range = current_guess - 1;
        }
    }
}

void thinker_signal_handler(int sig, siginfo_t *info, void *context) {
    if (sig == SIGRTMIN) {
        current_guess = info->si_value.sival_int;
        attempts++;
        printf("Thinker (PID %d): Received guess %d\n", getpid(), current_guess);
        fflush(stdout);
        
        // Отправляем не просто да/нет, а информацию о том, больше или меньше
        union sigval response;
        if (current_guess == guessed_number) {
            kill(info->si_pid, SIGUSR1);
        } else {
            response.sival_int = (current_guess < guessed_number); // 1 если нужно больше, 0 если меньше
            sigqueue(info->si_pid, SIGUSR2, response);
        }
    }
}

void play_guesser(int max_number, pid_t thinker_pid) {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = guesser_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    printf("Guesser (PID %d): Starting game (Thinker PID: %d)\n", getpid(), thinker_pid);
    fflush(stdout);

    srand(time(NULL) ^ getpid());
    min_range = 1;
    max_range = max_number;
    attempts = 0;
    game_over = 0;

    while (!game_over && attempts < MAX_ATTEMPTS) {
        current_guess = min_range + (max_range - min_range) / 2; // Бинарный поиск

        printf("Guesser (PID %d): Trying %d (range %d-%d)\n", getpid(), current_guess, min_range, max_range);
        fflush(stdout);

        union sigval value;
        value.sival_int = current_guess;
        if (sigqueue(thinker_pid, SIGRTMIN, value) == -1) {
            perror("sigqueue failed");
            exit(1);
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigsuspend(&mask);
    }
}

void play_thinker(int max_number, pid_t guesser_pid) {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = thinker_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGRTMIN, &sa, NULL);

    srand(time(NULL) ^ getpid());
    guessed_number = 1 + rand() % max_number;
    printf("Thinker (PID %d): I'm thinking of a number between 1 and %d (shh, it's %d)\n", 
           getpid(), max_number, guessed_number);
    fflush(stdout);

    while (!game_over) {
        sigset_t mask;
        sigemptyset(&mask);
        sigsuspend(&mask);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <max_number>\n", argv[0]);
        return 1;
    }

    int max_number = atoi(argv[1]);
    if (max_number < 1) {
        fprintf(stderr, "Max number must be at least 1\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    for (int game = 0; game < 10; game++) {
        if (pid == 0) {
            // Дочерний процесс
            if (game % 2 == 0) {
                play_thinker(max_number, getppid());
            } else {
                play_guesser(max_number, getppid());
            }
        } else {
            // Родительский процесс
            if (game % 2 == 0) {
                play_guesser(max_number, pid);
            } else {
                play_thinker(max_number, pid);
            }
        }

        guessed_number = 0;
        attempts = 0;
        game_over = 0;
        current_guess = 0;
        min_range = 1;
        max_range = max_number;
    }

    if (pid > 0) {
        kill(pid, SIGTERM);
        wait(NULL);
    }

    return 0;
}
