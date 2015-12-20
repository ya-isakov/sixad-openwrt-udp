/*
 * sixad-sixaxis.cpp
 *
 * This file is part of the QtSixA, the Sixaxis Joystick Manager
 * Copyright 2008-10 Filipe Coelho <falktx@gmail.com>
 *
 * QtSixA can be redistributed and/or modified under the terms of the GNU General
 * Public License (Version 2), as published by the Free Software Foundation.
 * A copy of the license is included in the QtSixA source code, or can be found
 * online at www.gnu.org/licenses.
 *
 * QtSixA is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 */

#include "shared.h"
#include "sixaxis.h"
#include "uinput.h"

#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <list>

int csk = 0;
int isk = 1;
int debug;
std::list<int> clients;

int led_n;
bool old_rumble_mode;

volatile bool active = false;
volatile int weak = 0;
volatile int strong = 0;
volatile int timeout = 0;
bool msg = true;

//struct uinput_fd *ufd;

/*static void uinput_listen()
{
        struct input_event event;
        struct uinput_ff_upload upload;
        struct uinput_ff_erase erase;
        struct rumble_effect effects[MAX_RUMBLE_EFFECTS];
        int i;

        //reset effects
        for (i=0; i<MAX_RUMBLE_EFFECTS; i++) {
            effects[i].id = -2;
        }

        while (!io_canceled()) {
                if (read(ufd->js, &event, sizeof event) != sizeof event) {
                        syslog(LOG_INFO, "uinput_listen::Error on uinput read");
                        continue;
                }

                if (debug) syslog(LOG_INFO, "GOT event :: type is %i; code is %i; value is %i\n", event.type, event.code, event.value);

                switch (event.type) {
                case EV_UINPUT:
                        switch (event.code) {
                        case UI_FF_UPLOAD:
                                memset(&upload, 0, sizeof(upload));
                                upload.request_id = event.value;
                                if (ioctl(ufd->js, UI_BEGIN_FF_UPLOAD, &upload) < 0) {
                                        syslog(LOG_ERR, "uinput_listen::Error on ff upload begin");
                                }
                                weak = upload.effect.u.rumble.weak_magnitude/0x101;
                                strong = upload.effect.u.rumble.strong_magnitude/0x101;
                                timeout = upload.effect.replay.length/0x101;
                                if (!old_rumble_mode) {
                                    for (i=0; i<MAX_RUMBLE_EFFECTS; i++) {
                                        if (effects[i].id == -2) {
                                            effects[i].id = upload.effect.id;
                                            effects[i].weak = weak;
                                            effects[i].strong = strong;
                                            effects[i].timeout = timeout;
                                            if (debug) syslog(LOG_INFO, "going for effect #%i", i);
                                            break;
                                        }
                                    }
                                } else {
                                    active = true;
                                }
                                upload.retval = 0;
                                if (ioctl(ufd->js, UI_END_FF_UPLOAD, &upload) < 0) {
                                        syslog(LOG_ERR, "uinput_listen::Error on ff upload end");
                                }
                                break;
                        case UI_FF_ERASE:
                                memset(&erase, 0, sizeof(erase));
                                erase.request_id = event.value;
                                if (ioctl(ufd->js, UI_BEGIN_FF_ERASE, &erase) < 0) {
                                        syslog(LOG_ERR, "uinput_listen::Error on ff erase begin");
                                }
                                if (!old_rumble_mode) {
                                    for (i=0; i<MAX_RUMBLE_EFFECTS; i++) {
                                        if (effects[i].id == event.value) {
                                            effects[i].id = -2;
                                            break;
                                        }
                                    }
                                } else {
                                    weak = 0;
                                    strong = 0;
                                    timeout = 0;
                                    active = true;
                                }
                                erase.retval = 0;
                                if (ioctl(ufd->js, UI_END_FF_ERASE, &erase) < 0) {
                                        syslog(LOG_ERR, "uinput_listen::Error on ff erase end");
                                }
                                break;
                        default:
                                break;
                        }
                        break;
                case EV_FF:
                        if (event.value) {
                            for (i=0; i<MAX_RUMBLE_EFFECTS; i++) {
                                if (effects[i].id == event.code) {
                                    do_rumble(csk, led_n, effects[i].weak, effects[i].strong, effects[i].timeout);
                                    if (debug) syslog(LOG_INFO, "RUMBLE now :: %i|%i|%i", effects[i].weak, effects[i].strong, effects[i].timeout);
                                    break;
                                }
                            }
                        } else {
                           if (!old_rumble_mode) {
                                do_rumble(csk, led_n, 0, 0, 0);
                                if (debug) syslog(LOG_INFO, "RUMBLE clean");
                           }
                        }
                        break;
                default:
                        break;
                }
        }

        pthread_exit((void*)1);
}*/

static void rumble_listen()
{
    while (!io_canceled()) {
        if (active) {
            do_rumble(csk, led_n, weak, strong, timeout);
            active = false;
        } else
            usleep(5000);
    }

    pthread_exit((void*)1);
}

static int get_time()
{
  timespec tp;
  if (!clock_gettime(CLOCK_MONOTONIC, &tp)) {
    return tp.tv_sec/60;
  } else {
    return -1;
  }
}

static void process_sixaxis(struct device_settings settings, const char *mac)
{
    int br;
    unsigned char buf[128], udpbuf[64];

    int last_time_action = get_time();

        br = read(isk, buf, sizeof(buf));
        if (msg) {
            syslog(LOG_INFO, "Connected 'PLAYSTATION(R)3 Controller (%s)' [Battery %02X]", mac, buf[31]);
            msg = false;
        }

        if (settings.timeout.enabled) {
            int current_time = get_time();
            if (was_active()) {
                last_time_action = current_time;
                set_active(false);
            } else if (current_time-last_time_action >= settings.timeout.timeout) {
                syslog(LOG_INFO, "Sixaxis was not in use, and timeout reached, disconneting...");
                sig_term(0);
                return;
            }
        }

        if (br < 0) {
            return;
        } else if (br==13 && buf[0]==0xa1 && buf[1]==0x01 && buf[2]==0x00) { //only continue if we've got a Sixaxis
/*            if (settings.auto_disconnect && buf[34] != 0x00 && buf[34] < 0xB5) {
                syslog(LOG_INFO, "Sixaxis out of reach, auto-disconneting now...");
                sig_term(0);
                break;
            }*/
//            if (settings.joystick.enabled) do_joystick(0, buf, settings.joystick);
//            if (settings.input.enabled) do_input(0, buf, settings.input);
              std::list<int>::iterator i;
              for(i = clients.begin(); i != clients.end(); ++i) {
                  bzero(&udpbuf, sizeof(udpbuf));
                  udpbuf[0] = 0; //Ordinal number of gamepad
                  udpbuf[1] = 2; //Magic num
                  memmove(udpbuf+10, buf+3, 15);
                  send(*i, udpbuf, sizeof(udpbuf), 0);
              };

        } else if (br==13 && buf[0]==0xa1 && buf[1]==0x01 && buf[2]==0xff) {
            if (debug) syslog(LOG_ERR, "Got 0xff Sixaxis buffer, ignored");
        } else if (buf[0]==0xa1 && buf[1]==0x01 && buf[2]==0x00) {
            syslog(LOG_ERR, "Bad Sixaxis buffer (out of battery?), disconneting now...");
	    sig_term(0);
            return;
        } else {
            if (debug) syslog(LOG_ERR, "Non-Sixaxis packet received and ignored (0x%02x|0x%02x|0x%02x)", buf[0], buf[1], buf[2]);
        }

}

int process_udp(int fd) {
    char buf[6];
    int clientfd;
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    int n = recvfrom(fd, buf, 6, 0, (struct sockaddr *)&cliaddr, &len);
    switch (buf[1]) {
       case 0x00:
          syslog(LOG_INFO, "Connected client %s", inet_ntoa(cliaddr.sin_addr));
          buf[2] = 0x02; // Connected
          buf[3] = 0x00; // Disconnected
          buf[4] = 0x00; // Disconnected
          buf[5] = 0x00; // Disconnected
          sendto(fd, buf, 6, 0, (struct sockaddr *)&cliaddr, len);
          cliaddr.sin_port = 26761;
          clientfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
          connect(clientfd, (struct sockaddr *)&cliaddr, len);
          clients.push_back(clientfd);
          syslog(LOG_INFO, "Clients: %d", clients.size());
          break;
       case 0x01:
          syslog(LOG_INFO, "Rumble gamepad N%d: 0x%02x, 0x%02x", buf[0], buf[2], buf[3]);
          break;
       case 0x02:
          syslog(LOG_INFO, "Unknown, gamepad N%d: 0x%02x, 0x%02x", buf[0], buf[2], buf[3]);
          break;
       case 0x03:
          syslog(LOG_INFO, "Get global config");
          break;
       case 0x04:
          syslog(LOG_INFO, "Set global config");
          break;
    }
}

int main(int argc, char *argv[])
{
    struct pollfd p[4];
    struct timespec timeout;
    struct device_settings settings;
    struct sigaction sa;
//    pthread_t uinput_listen_thread, rumble_listen_thread;
    sigset_t sigs;
    short events;
    int sockfd;
    struct sockaddr_in servaddr;

    if (argc < 3) {
        std::cout << "Running " << argv[0] << " requires 'sixad'. Please run sixad instead" << std::endl;
        return 1;
    }

    const char *mac = argv[1];
    debug = atoi(argv[2]);

    open_log("sixad-sixaxis");
    syslog(LOG_INFO, "started");
    settings = init_values(mac);
    settings.remote.enabled = false;

    //ufd = uinput_open(DEV_TYPE_SIXAXIS, mac, settings);

/*    if (ufd->js < 0 || ufd->mk < 0) {
        return 1;
    } else if (ufd->js == 0 && ufd->mk == 0) {
        syslog(LOG_ERR, "sixaxis config has no joystick or input mode selected - please choose one!");
        return 1;
    }*/

    enable_sixaxis(csk);
    led_n = set_sixaxis_led(csk, settings.led, settings.rumble.enabled);

    if (settings.rumble.enabled) {
      old_rumble_mode = settings.rumble.old_mode;
/*      if (pthread_create(&uinput_listen_thread, NULL, (void *(*)(void *))uinput_listen, NULL)) {
              syslog(LOG_ERR, "error starting uinput listen thread");
              return 1;
      }
      if (settings.rumble.old_mode) {
          if (pthread_create(&rumble_listen_thread, NULL, (void *(*)(void *))rumble_listen, NULL)) {
              syslog(LOG_ERR, "error starting rumble listen thread");
              return 1;
          }
      }*/
    }

    sigfillset(&sigs);
    sigdelset(&sigs, SIGCHLD);
    sigdelset(&sigs, SIGPIPE);
    sigdelset(&sigs, SIGTERM);
    sigdelset(&sigs, SIGINT);
    sigdelset(&sigs, SIGHUP);

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_NOCLDSTOP;

    sa.sa_handler = sig_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    p[0].events = POLLIN | POLLERR | POLLHUP;
    p[1].events = POLLIN | POLLERR | POLLHUP;
    p[2].events = POLLIN | POLLERR | POLLHUP;
//    p[3].events = POLLIN | POLLERR | POLLHUP;
    
    p[0].fd = 0;
    p[1].fd = 1;
    p[2].fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(26760);
    bind(p[2].fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
//    syslog(LOG_INFO, "UDP %d", p[2].fd);

/*    p[2].fd = ufd->js;
    p[3].fd = ufd->mk;

    if (!ufd->js)
      p[2].fd = ufd->mk;*/

//    int idx = (ufd->js && ufd->mk) ? 4 : 3;
    int idx = 3;

    while (!io_canceled()) {
        int i;

        for (i = 0; i < idx; i++)
            p[i].revents = 0;

        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;

        int res = ppoll(p, idx, &timeout, &sigs);
        if (res < 0) {
            sig_term(0);
            break;
        };
        if (res == 0) continue;
//        syslog(LOG_INFO, "UDP events %d", p[2].revents);

        if (p[2].revents & POLLIN) {
//            syslog(LOG_INFO, "Got UDP");
            process_udp(p[2].fd);
        };
        if ((p[1].revents & POLLIN) && (clients.size() > 0)) {
//            syslog(LOG_INFO, "begin");
            process_sixaxis(settings, mac);
        }

        events = p[0].revents | p[1].revents | p[2].revents;// | p[3].revents;

        if (events & (POLLERR | POLLHUP | POLLNVAL)) {
            sig_term(0);
            break;
        }
    }

/*    if (settings.rumble.enabled) {
      if (pthread_cancel(uinput_listen_thread)) {
        if (pthread_join(uinput_listen_thread, NULL)) {
          syslog(LOG_ERR, "Error canceling uinput listen thread");
        }
      }
      if (settings.rumble.old_mode) {
          if (pthread_cancel(rumble_listen_thread)) {
            if (pthread_join(rumble_listen_thread, NULL)) {
              syslog(LOG_ERR, "Error canceling rumble listen thread");
            }
          }
      }
    }*/

    if (debug) syslog(LOG_INFO, "Closing uinput...");

/*    if (settings.joystick.enabled) {
        uinput_close(ufd->js, debug);
    }
    if (settings.input.enabled) {
        uinput_close(ufd->mk, debug);
    }*/

//    delete ufd;
    
    do_rumble(csk, 10, 0xff, 0xff, 0x01);

    shutdown(isk, SHUT_RDWR);
    shutdown(csk, SHUT_RDWR);

    if (debug) syslog(LOG_INFO, "Done");

    return 0;
}
