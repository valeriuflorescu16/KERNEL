#include "philosophers.h"

// spawn 16 philosophers using fork- child processes
// The Problem : Each philosopher must alternately THINK and EAT.
//However, a philosopher can only eat spaghetti when they have both left and right forks.
// Each fork can be held by only one philosopher and so a philosopher can use the fork
//only if it is not being used by another philosopher.
// After an individual philosopher finishes eating, they need to put down both forks so that
//the forks become available to others.
// A philosopher can only take the fork on their right or the one on their left as they become
//available and they cannot start eating before getting both forks.
//Eating is not limited by the remaining amounts of spaghetti or stomach space;
//an infinite supply and an infinite demand are assumed.
//The problem is how to design a discipline of behavior (a concurrent algorithm) such that no philosopher will starve;
//i.e., each can forever continue to alternate between eating and thinking,
//assuming that no philosopher can know when others may want to eat or think.
// i = 0 -> forks[15], forks[0]
// i = 2 -> forks[1], forks[2]
// i = 3 -> forks[2], forks[3]  so for philosopher[i], forks[i] on the right hand side, forks[i-1] on the left
// the freerightfork and freeleftfork are functions that make the left and right fork available when called
// the PhiloID(ID) function prints out Philosopher ID, used for printing out which philosopher is doing the action
// think and eat are simple functions printing out when the current philosopher is either thinking or eating
// requestleftfork and requestrightfork are communicating with the waiter, asking for the left and right fork
// which may not be available depending on either if they are reserved by another philosopher
// or if they are used by another philosopher;
// giveleftfork and giverightfork are called to hand the forks back to the waiter- this is done depending on which philosopher
// is the current owner of the forks, as once a fork is taken by a philosopher, the philosopher is assigned as the owner 
// of the respective fork;
// the philosopher function is communicating with the waiter function through 2 pipes - a pipe from philosopher to the waiter
// and a pipe from the waiter to the philosopher; through these pipes, the philosopher can request a fork from the waiter
// through the 2 requestforks functions I have created, or he can hand a fork back if he is the owner of it;
// they're both infinite loops so that the program runs forever.


// MUTUAL EXCLUSION + STARVATION


// MUTUAL EXCLUSION is prevented as once a fork is taken by a philosopher, this forks becomes unavailable - this was done through
// a boolean in my forks_t struct in philosophers.h - bool available; so no 2 philosophers can hold the same fork at the same time-
// another philosopher can pick up a fork a philosopher has once givefork() is called on the owner of that respective fork

// STARVATION is prevented as once the philosophers pick up a left fork, the right fork for each is then reserved - so when 
// philosopher 1 picks up his left fork, his left fork is then reserved for him to pick up the next time he interacts with the waiter
// so philosopher 2 will not be able to pick up a left fork until philosopher 1 releases his right fork
// so then, you will realise that on the first go, the odd philosophers will pick up a left fork ( 1, 3, 5 , .., 15), whilst the even
// ones will be rejected a left fork; then, the odd ones will pick up a right fork, eat, release the forks and think, and then the even
// ones will pick up the forks in the same manner;


#define FORKSNO 16

forks_t forks[FORKSNO];

void freerightfork(int i)
{

    forks[i].available = true;
}

void freeleftfork(int i)
{

    if (((i - 1) == -1))
    {
        forks[15].available = true;
    }
    else
    {
        forks[i - 1].available = true;
    }
}


void PhiloID(int ID)
{
    char idbuffer[3];
    itoa(idbuffer, (ID + 1));
    write(STDOUT_FILENO, "Philosopher", 12);
    write(STDOUT_FILENO, idbuffer, 3);
}

void think(int ID)
{
    PhiloID(ID);
    write(STDOUT_FILENO, "is thinking \n", 14);
    yield();
}

void eat(int ID)
{
    PhiloID(ID);
    write(STDOUT_FILENO, "is eating \n", 12);
}

void requestleftfork(int readfd, int writefd, int ID)
{

    char reading[16];

    // request left fork
    bool leftforkreceived = false;
    while (leftforkreceived == false)
    {
        write(writefd, "RL", 2);
        read(readfd, reading, 1);

        if (0 == strcmp(reading, "y"))
        {
            PhiloID(ID);
            write(STDOUT_FILENO, "picked up left fork \n", 22);
            leftforkreceived = true;
        }
        else
        {
            write(STDOUT_FILENO, "Left fork not available yet \n", 30);
            yield();
        }
    }
}

void requestrightfork(int readfd, int writefd, int ID)
{
    char reading[16];

    //request right fork;
    bool rightforkreceived = false;
    while (rightforkreceived == false)
    {
        write(writefd, "RR", 2);
        read(readfd, reading, 1);

        if (0 == strcmp(reading, "y"))
        {
            PhiloID(ID);
            write(STDOUT_FILENO, "picked up right fork \n", 23);
            rightforkreceived = true;
        }
        else
        {
            write(STDOUT_FILENO, "Right fork not available yet \n", 31);
            yield();
        }
    }
}

void giveleftfork(int readfd, int writefd, int ID)
{

    char reading[16];
    bool releasedfork = false;
    //give left fork to waiter;
    while (releasedfork == false)
    {
        write(writefd, "GL", 2);
        read(readfd, reading, 1);

        if (0 == strcmp(reading, "o"))
        {
            PhiloID(ID);
            write(STDOUT_FILENO, "released left fork \n", 21);
            releasedfork = true;
        }
        else
        {
            yield();
        }
    }
}

void giverightfork(int readfd, int writefd, int ID)
{
    char reading[16];
    bool releasedfork = false;
    //give right fork to waiter;

    while (releasedfork == false)
    {
        write(writefd, "GR", 2);
        read(readfd, reading, 1);
        if (0 == strcmp(reading, "o"))
        {
            PhiloID(ID);
            write(STDOUT_FILENO, "released right fork \n", 22);
            releasedfork = true;
        }
        else
        {
            yield();
        }
    }
}

void philosopher(int readfd, int writefd, int ID)
{

    char reading[16];

    while (1)
    {
        requestleftfork(readfd, writefd, ID);
        yield();
        requestrightfork(readfd, writefd, ID);
        yield();
        eat(ID);
        yield();
        giverightfork(readfd, writefd, ID);
        yield();
        giveleftfork(readfd, writefd, ID);
        yield();
        think(ID);
        // comment exit(EXIT_SUCCESS); out so that the program will run forever;
        // otherwise it will stop and only the waiter will run once all the philosophers finished thinking;
        exit(EXIT_SUCCESS);
    }
}

void waiter(int waiter_readfd[FORKSNO], int waiter_writefd[FORKSNO])
{
    char reading[16];

    while (1)
    {
        for (int i = 0; i < FORKSNO; i++)
        {
            read(waiter_readfd[i], reading, 2);
            
            if (0 == strcmp(reading, "RL"))
            {
                if ((i - 1) != -1)
                {
                    if ((forks[i - 1].available == true) && (forks[i - 1].reserved == false))
                    {
                        write(waiter_writefd[i], "y", 1);
                        forks[i - 1].available = false;
                        forks[i].reserved = true;
                        forks[i - 1].owner = i;
                    }
                    
                }
                else if ((i - 1) == -1)
                {
                    if ((forks[15].available == true) && (forks[15].reserved == false))
                    {
                        write(waiter_writefd[i], "y", 1);
                        forks[15].available = false;
                        forks[i].reserved = true;
                        forks[15].owner = i;
                    }
                    
                }
            }

            else if (0 == strcmp(reading, "RR"))
            {
                if (forks[i].available == true)
                {
                    write(waiter_writefd[i], "y", 1);
                    forks[i].available = false;
                    forks[i].owner = i;
                }
                
            }

            else if (0 == strcmp(reading, "GL"))
            {
                if (((i - 1) != -1))
                {
                    if (forks[i - 1].owner == i)
                    {
                        write(waiter_writefd[i], "o", 1);
                        forks[i].reserved = false;
                        freeleftfork(i);
                    }
                   
                }
                else if (((i - 1) == -1))
                {
                    if (forks[15].owner == i)
                    {
                        write(waiter_writefd[i], "o", 1);
                        forks[i].reserved = false;
                        freeleftfork(i);
                    }
                   
                }
            }

            else if (0 == strcmp(reading, "GR"))
            {
                if (forks[i].owner == i)
                {
                    write(waiter_writefd[i], "o", 1);
                    freerightfork(i);
                }
                
            }
        }
        yield();
    }
}

void forksavailable()
{
    for (int i = 0; i < FORKSNO; i++)
    {
        forks[i].available = true;
    }
}

void main_philosophers()
{

    int waiter_readfd[FORKSNO];
    int waiter_writefd[FORKSNO];
    int readfd;
    int writefd;

    for (int i = 0; i < FORKSNO; i++)
    {

        int fd_w_to_p[2];
        int fd_p_to_w[2];

        pipe(fd_w_to_p);
        pipe(fd_p_to_w);

        waiter_readfd[i] = fd_p_to_w[0];
        writefd = fd_p_to_w[1];

        waiter_writefd[i] = fd_w_to_p[1];
        readfd = fd_w_to_p[0];

        int pid = fork();
        if (pid == 0)
        {
            philosopher(readfd, writefd, i);
            exit(EXIT_SUCCESS);
        }
    }

    for (int i = 0; i < FORKSNO; i++)
    {
        forks[i].owner = -1;
    }

    for (int i = 0; i < FORKSNO; i++)
    {
        forks[i].reserved = false;
    }

    forksavailable();
    waiter(waiter_readfd, waiter_writefd);

    exit(EXIT_SUCCESS);
}
