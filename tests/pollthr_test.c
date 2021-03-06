/*
*****************************************************
**      CodeChix ONF Driver (LibCCOF)
**      codechix.org - May the code be with you...
**              Sept. 15, 2013
*****************************************************
**
** License:        Apache 2.0 (ONF requirement)
** Version:        0.0
** LibraryName:    LibCCOF
** GLIB License:   GNU LGPL
** Description:    Test harness for pollthread for LibCCOF
** Assumptions:    N/A
** Testing:        N/A
** Authors:        Deepa Karnad Dhurka
**
*****************************************************
*/

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>

#include "cc_pollthr_mgr.h"
#include "cc_of_global.h"
#include "cc_log.h"

#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif

#define LIBLOG_SIZE 4096

extern cc_of_global_t cc_of_global;

/* Fixture data */
typedef struct test_data_ {
    adpoll_thread_mgr_t tp_data;    
    char                liblog[LIBLOG_SIZE];
} test_data_t;
    
/*  Function: pollthread_start
 *  This is a fixture funxtion.
 *  Initialize a poll thread with 10 max sockets
 *  and 1 max pipes
 */
static void
pollthread_start(test_data_t *tdata,
                 gconstpointer tudata)
{
    adpoll_thread_mgr_t *temp_mgr_p = NULL;
    char *temp_liblog = NULL;

    /* initialize and setup debug and logfile */
    cc_of_global.oflog_fd = NULL;
    cc_of_global.oflog_file = malloc(sizeof(char) *
                                     LOG_FILE_NAME_SIZE);
    g_mutex_init(&cc_of_global.oflog_lock);
//    cc_of_debug_toggle(TRUE);    //enable if debugging test code
    cc_of_log_toggle(TRUE);
    cc_of_global.ofut_enable = TRUE;
    
    /* create new thread manager */
    temp_mgr_p = adp_thr_mgr_new((char *)tudata, 10, 1);
    
    /* need to copy the contents to tp because tp is
       allocated by test framework - the location needs
       to be preserved and not over-written.
    */
    // bad idea - save the pointer to mgr instead
    g_memmove(&tdata->tp_data, temp_mgr_p, sizeof(adpoll_thread_mgr_t));

    g_test_message("add del mutex %p cv %p", tdata->tp_data.add_del_pipe_cv_mutex,
                   tdata->tp_data.add_del_pipe_cv_cond);                   
    
    //use cc_of_log_read() to read the contents of log file
    temp_liblog = cc_of_log_read();

    if (temp_liblog) {
        g_test_message("file content size: %u",
                       (uint)strlen(temp_liblog));
        memcpy(tdata->liblog, temp_liblog, LIBLOG_SIZE);
    
        g_free(temp_liblog);
    }
    
    g_free(temp_mgr_p);

}

/* Function that tears down the test and cleans up */
static void
pollthread_end(test_data_t *tdata,
               gconstpointer tudata UNUSED)
{
    cc_of_log_toggle(FALSE);
    cc_of_global.ofut_enable = FALSE;

    g_test_message("In POLLTHREAD_END");
    
    g_test_message("add del mutex %p cv %p", tdata->tp_data.add_del_pipe_cv_mutex,
                   tdata->tp_data.add_del_pipe_cv_cond);                   
    
    adp_thr_mgr_free(&(tdata->tp_data));
    g_mutex_clear(&cc_of_global.oflog_lock);
    g_free(cc_of_global.oflog_file);
}

void
regex_one_compint(char *liblog, //size LIBLOG_SIZE
                  char *pattern,
                  int match_iter,
                  int compareval)
{
    GRegex *regex = NULL;
    GMatchInfo *match_info;

    regex = g_regex_new (pattern, G_REGEX_MULTILINE,
                         0, NULL);
    if (!(g_regex_match(regex, liblog, 0, &match_info))) {
        g_test_message("no successful match");
        g_test_fail();
    }
        
    if (g_match_info_matches (match_info))
    {
        gchar *word = g_match_info_fetch (match_info, match_iter);
        g_assert_cmpint(atoi(word), ==, compareval);
        g_free (word);
    }
    g_match_info_free (match_info);
    g_regex_unref (regex);
}

/* check the basic health upon bringing up poll thread */
// test the fixture data is setup correctly
// test the basic health of primary and data pipe
// test the tear down of poll thread (self destruct)
// test the synchronization in adding/deleting fds
static void
pollthread_tc_1(test_data_t *tdata,
                gconstpointer tudata)
{
    if (tdata != NULL) {
        g_test_message("test - name of thread is %s", (char *)tudata);
        g_assert_cmpstr(tdata->tp_data.tname, ==, (char *)tudata);

        g_test_message("add del mutex %p cv %p", tdata->tp_data.add_del_pipe_cv_mutex,
                       tdata->tp_data.add_del_pipe_cv_cond);                   
        
        g_test_message("test - 2 pipes created; 4 pipe fds");
        g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
        g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 4);
        
        g_test_message("test - num_avail_sockfd is 10");
        g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                        ==, 10);

        g_test_message("test - output of log follows");
        g_test_message("%s",tdata->liblog);
        g_test_message("test - output of log ends");

        g_test_message("test - num fd_entry_p in fd_list is 1");
        regex_one_compint(
            tdata->liblog,
            "fd_list has ([0-9]+) entries SETUP PRI PIPE",
            1, 1);

        g_test_message("test - value of primary pipe read fd");
        regex_one_compint(
            tdata->liblog,
            "pipe fds created.*([0-9])..([0-9])..PRIMARY",
            1, adp_thr_mgr_get_pri_pipe_rd(&tdata->tp_data));
        
        g_test_message("test - value of primary pipe write fd");
        regex_one_compint(
            tdata->liblog,
            "pipe fds created.*([0-9])..([0-9])..PRIMARY",
            2, adp_thr_mgr_get_pri_pipe_wr(&tdata->tp_data));
        

        g_test_message("test - num fd_entry_p in fd_list is 2");        
        regex_one_compint(
            tdata->liblog,
            "fd_list has ([0-9]) entries ADD_FD",
            1, 2);

        g_test_message("test - value of data pipe read fd");
        regex_one_compint(
            tdata->liblog,
            "pipe fds created.*([0-9])..([0-9])..ADD-ON",
            1, adp_thr_mgr_get_data_pipe_rd(&tdata->tp_data));

        g_test_message("test - value of data pipe write fd");
        regex_one_compint(
            tdata->liblog,
            "pipe fds created.*([0-9])..([0-9])..ADD-ON",
            2, adp_thr_mgr_get_data_pipe_wr(&tdata->tp_data));


        g_test_message("test - num_pollfds");
        regex_one_compint(
            tdata->liblog,
            "num pollfds is ([0-9]+) after ADD_FD",
            1, 2);

    } else {
        g_test_message("fixture data invalid");
        g_test_fail();
    }
}


/* pollin function for new pipe with newly defined message
 *  definition
 */
typedef struct test_fd_rd_wr_data_ {
    char msg[32];
} test_fd_rd_wr_data_t;

void
test_pipe_in_process_func(char *tname,
                          adpoll_fd_info_t *data_p,
                          adpoll_send_msg_htbl_info_t *unused_data UNUSED)
{
    test_fd_rd_wr_data_t in_data;
    
    g_test_message("test - %s: thread name sent to callback is thread_tc_2",
                   __FUNCTION__);
    g_assert_cmpstr(tname, ==, "thread_tc_2");
    
    g_test_message("test - %s: message received by polling thread on fd "
                   "is \"hello 1..2..3\"", __FUNCTION__);
    read(data_p->fd, &in_data, sizeof(in_data));
    g_assert_cmpstr(in_data.msg, ==, "hello 1..2..3");

    g_test_message("test - %s: fd info is valid", __FUNCTION__);
    g_assert_cmpint(data_p->fd_type, ==, PIPE);
    g_assert(data_p->pollfd_entry_p != NULL);
    g_assert(data_p->pollfd_entry_p->events & POLLIN);
    g_assert(!(data_p->pollfd_entry_p->events & POLLOUT));
}



//tc_2 - exercise the primary pipe - add/del pipe
//     - exercise the callback function sent for pollin/pollout
//     - exercise the poll on fd to call the callback function
//     - test:
//     - create a new pipe, pass a pollin func
//     - delete the pipe
static void
pollthread_tc_2(test_data_t *tdata,
                gconstpointer tudata UNUSED)
{
    adpoll_thr_msg_t add_pipe_msg;
    adpoll_thr_msg_t del_pipe_msg;
    test_fd_rd_wr_data_t test_msg;
    char *temp_liblog = NULL;
    int wr_fd;
    
    /* add_del_fd - add pipe with pollin func */
    /* test pipe */
    /* add_del_fd - delete pipe */
    add_pipe_msg.fd_type = PIPE;
    add_pipe_msg.fd_action = ADD_FD;
    add_pipe_msg.poll_events = POLLIN;
    add_pipe_msg.pollin_func = &test_pipe_in_process_func;
    add_pipe_msg.pollout_func = NULL;

    /* clear the log */
    cc_of_log_clear();
    
    wr_fd = adp_thr_mgr_add_del_fd(&tdata->tp_data, &add_pipe_msg);

    g_assert (wr_fd != -1);
    /* send test message */
    sprintf(test_msg.msg, "hello 1..2..3");

    write(wr_fd, &test_msg, sizeof(test_msg));
    
//
//    g_test_message("test - output of log follows");
//    g_test_message("%s",tdata->liblog);
//    g_test_message("test - output of log ends");

    temp_liblog = cc_of_log_read();

    g_test_message("Test health after add test pipe");

    /* test the health */
    g_test_message("test - 3 pipes created; 6 pipe fds");
    g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
    g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 6);
    
    g_test_message("test - num_avail_sockfd is 10");
    g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                    ==, 10);
    
    g_test_message("test - num fd_entry_p in fd_list is 3");
    regex_one_compint(
        temp_liblog,
        "fd_list has ([0-9]+) entries ADD_FD",
        1, 3);

    g_test_message("test - num_pollfds");
    regex_one_compint(
        temp_liblog,
        "num pollfds is ([0-9]+) after ADD_FD",
        1, 3);
    
    /* clear the log */
    cc_of_log_clear();

    del_pipe_msg.fd = wr_fd;
    del_pipe_msg.fd_type = PIPE;
    del_pipe_msg.fd_action = DELETE_FD;
    del_pipe_msg.poll_events = 0;
    del_pipe_msg.pollin_func = NULL;
    del_pipe_msg.pollout_func = NULL;

    adp_thr_mgr_add_del_fd(&tdata->tp_data, &del_pipe_msg);

    temp_liblog = cc_of_log_read();    

    g_test_message("Test health after del test pipe");
    
    g_test_message("test - 2 pipes created; 4 pipe fds");
    g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
    g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 4);
    
    g_test_message("test - num_avail_sockfd is 10");
    g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                    ==, 10);
    
    g_test_message("test - num fd_entry_p in fd_list is 2");
    regex_one_compint(
        temp_liblog,
        "fd_list has ([0-9]+) entries DELETE_FD",
        1, 2);

    g_test_message("test - num_pollfds");
    regex_one_compint(
        temp_liblog,
        "num pollfds is ([0-9]+) after DELETE_FD",
        1, 2);
}


void
test_socket_in_process_func(char *tname,
                          adpoll_fd_info_t *data_p,
                          adpoll_send_msg_htbl_info_t *unused_data UNUSED)
{
    test_fd_rd_wr_data_t in_data;    

    g_test_message("test - %s: thread name sent to callback is thread_tc_3",
                   __FUNCTION__);
    g_assert_cmpstr(tname, ==, "thread_tc_3");

    g_test_message("test - %s: message received by polling thread on fd "
                   "is \"hello 1..2..3..10\"", __FUNCTION__);
    read(data_p->fd, &in_data, sizeof(in_data));
    g_assert_cmpstr(in_data.msg, ==, "hello 1..2..3..10");

    g_test_message("test - %s: fd info is valid", __FUNCTION__);
    g_assert_cmpint(data_p->fd_type, ==, SOCKET);
    g_assert(data_p->pollfd_entry_p != NULL);
    g_assert(data_p->pollfd_entry_p->events & POLLIN);
    g_assert(!(data_p->pollfd_entry_p->events & POLLOUT));
}


//tc_3 - exercise receive path of data
//     - exercise the primary pipe - add/del socket
//     - create a pair of unix sockets
//     - with pollin and pollout func
//     - delete the socket
static void
pollthread_tc_3(test_data_t *tdata,
                gconstpointer tudata UNUSED)
{
    adpoll_thr_msg_t add_sock_msg;
    adpoll_thr_msg_t del_sock_msg;
    test_fd_rd_wr_data_t test_msg;
    char *temp_liblog = NULL;
    int wr_fd;
    int sv[2]; /* the pair of socket descriptors */
    int childpid;
    
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        CC_LOG_ERROR("socketpair failure");
        g_test_message("socketpair setup failed");
        g_test_fail();
    }

    childpid = fork();
    if (childpid == 0) {
        /* test the socket polling: send test message */
        sprintf(test_msg.msg, "hello 1..2..3..10");
        write(sv[1], &test_msg, sizeof(test_msg));
        exit(0);
    } else { /* parent */
        
    /* add_del_fd - add socket with pollin func */
    /* test socket */
    /* add_del_fd - delete socket */
        add_sock_msg.fd = sv[0];
        add_sock_msg.fd_type = SOCKET;
        add_sock_msg.fd_action = ADD_FD;
        add_sock_msg.poll_events = POLLIN;
        add_sock_msg.pollin_func = &test_socket_in_process_func;
        add_sock_msg.pollout_func = NULL;
        
        /* clear the log */
        cc_of_log_clear();
        
        wr_fd = adp_thr_mgr_add_del_fd(&tdata->tp_data, &add_sock_msg);
        
        g_assert (wr_fd != -1);
        g_assert_cmpint(wr_fd, ==, sv[0]);
        
    
//
//    g_test_message("test - output of log follows");
//    g_test_message("%s",tdata->liblog);
//    g_test_message("test - output of log ends");
        
        temp_liblog = cc_of_log_read();
        
        g_test_message("Test health after add test socket");

        g_test_message("test - 2 pipes created; 4 pipe fds");
        g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
        g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 4);
        
        g_test_message("test - num_avail_sockfd is 9");
        g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                        ==, 9);
    
        g_test_message("test - num fd_entry_p in fd_list is 3");
        regex_one_compint(
            temp_liblog,
            "fd_list has ([0-9]+) entries ADD_FD",
            1, 3);
        
        g_test_message("test - num_pollfds is 3");
        regex_one_compint(
            temp_liblog,
            "num pollfds is ([0-9]+) after ADD_FD",
            1, 3);
        
        wait(NULL); /* wait for child to die */
        
        /* sleep for sometime to allow the polling thread
           to process the receive
        */
        g_usleep(100000); //1 million microseconds is 1 sec

        /* clear the log */
        cc_of_log_clear();
        
        /* delete the socket */
        del_sock_msg.fd = sv[0];
        del_sock_msg.fd_type = SOCKET;
        del_sock_msg.fd_action = DELETE_FD;
        del_sock_msg.poll_events = 0;
        del_sock_msg.pollin_func = NULL;
        del_sock_msg.pollout_func = NULL;

        adp_thr_mgr_add_del_fd(&tdata->tp_data, &del_sock_msg);
        
        temp_liblog = cc_of_log_read();    

        g_test_message("Test health after add test socket");

        g_test_message("test - 2 pipes created; 4 pipe fds");
        g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
        g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 4);
        
        g_test_message("test - num_avail_sockfd is 10");
        g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                        ==, 10);
    
        g_test_message("test - num fd_entry_p in fd_list is 2");
        regex_one_compint(
            temp_liblog,
            "fd_list has ([0-9]+) entries DELETE_FD",
            1, 2);
        
        g_test_message("test - num_pollfds is 2");
        regex_one_compint(
            temp_liblog,
            "num pollfds is ([0-9]+) after DELETE_FD",
            1, 2);
    }
    close(sv[0]);
    close(sv[1]);
    return;
}

void
test_socket_out_process_func(char *tname,
                             adpoll_fd_info_t *data_p,
                             adpoll_send_msg_htbl_info_t *htbl_out_data)
{
    test_fd_rd_wr_data_t out_data;
    int out_data_size = 32;

    g_test_message("test - %s: thread name sent to callback is thread_tc_4",
                   __FUNCTION__);
    g_assert_cmpstr(tname, ==, "thread_tc_4");

    g_test_message("test - %s: fd info is valid", __FUNCTION__);
    g_assert_cmpint(data_p->fd_type, ==, SOCKET);
    g_assert(data_p->pollfd_entry_p != NULL);
    g_assert(!(data_p->pollfd_entry_p->events & POLLIN));
    g_assert(data_p->pollfd_entry_p->events & POLLOUT);
    g_assert(htbl_out_data != NULL);
    

    if (htbl_out_data->data_size < 32) {
        out_data_size = htbl_out_data->data_size;
    }
    
    g_memmove(out_data.msg, htbl_out_data->data, out_data_size);

    g_test_message("test - %s: writing to socket: \"%s\"",
                   __FUNCTION__, out_data.msg);
    
    write(data_p->fd, &out_data, sizeof(out_data));

    return;
}

//tc_4 - exercise the data pipe - the send path of data
//     - first add unix socket using pri pipe (tc_3)
//     - then send message on data pipe and test
//     - delete the unix socket
static void
pollthread_tc_4(test_data_t *tdata,
                gconstpointer tudata UNUSED)
{
    adpoll_thr_msg_t add_sock_msg;
    adpoll_thr_msg_t del_sock_msg;
    test_fd_rd_wr_data_t test_msg;
    char *temp_liblog = NULL;
    int rd_fd, wr_fd;
    int sv[2]; /* the pair of socket descriptors */
    char send_buf[100];
    char test_str[] = "hello 1..2..3..10..20";
    int status, childpid;

//    cc_of_debug_toggle(TRUE);    //enable if debugging test code
    
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        CC_LOG_ERROR("socketpair failure");
        g_test_message("socketpair setup failed");
        g_test_fail();
    }

    childpid = fork();
    if (childpid == 0) { /*child*/
        /* test the socket polling: send test message */
        read(sv[1], &test_msg, sizeof(test_msg));
        g_test_message("test - %s: received message "
                       "\"%s\"", __FUNCTION__, test_msg.msg);
        g_assert_cmpstr(test_msg.msg, ==, test_str);
        
        exit(0);
    } else { /* parent */
        
        /* add_del_fd - add socket with pollout func */
        /* test socket */
        /* add_del_fd - delete socket */
        add_sock_msg.fd = sv[0];
        add_sock_msg.fd_type = SOCKET;
        add_sock_msg.fd_action = ADD_FD;
        add_sock_msg.poll_events = POLLOUT;
        add_sock_msg.pollin_func = NULL;
        add_sock_msg.pollout_func = &test_socket_out_process_func;
        
        g_test_message("add data read fd: %d", sv[0]);

        rd_fd = adp_thr_mgr_add_del_fd(&tdata->tp_data, &add_sock_msg);
        
        g_assert (rd_fd != -1);
        g_assert_cmpint(rd_fd, ==, sv[0]);

        /* clear the log */
        cc_of_log_clear();
        
    g_test_message("test - output of log follows");
    g_test_message("%s",tdata->liblog);
    g_test_message("test - output of log ends");
        
        temp_liblog = cc_of_log_read();
        
        
        
        g_test_message("Test health after add test socket");

        g_test_message("test - 2 pipes created; 4 pipe fds");
        g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
        g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 4);
        
        g_test_message("test - num_avail_sockfd is 9");
        g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                        ==, 9);
    
        g_test_message("test - num fd_entry_p in fd_list is 3");
        regex_one_compint(
            temp_liblog,
            "fd_list has ([0-9]+) entries ADD_FD",
            1, 3);
        
        g_test_message("test - num_pollfds is 3");
        regex_one_compint(
            temp_liblog,
            "num pollfds is ([0-9]+) after ADD_FD",
            1, 3);
        
        /* clear the log */
        cc_of_log_clear();
        ((adpoll_send_msg_t *)send_buf)->hdr.msg_size =
            sizeof(adpoll_send_msg_t) + strlen(test_str) + 1;
        
        ((adpoll_send_msg_t *)send_buf)->hdr.fd = rd_fd;
            
        g_memmove(((adpoll_send_msg_t *)send_buf)->data,
                  test_str, strlen(test_str) + 1);
        
        wr_fd = adp_thr_mgr_get_data_pipe_wr(&tdata->tp_data);

        write(wr_fd, send_buf,
              ((adpoll_send_msg_t *)send_buf)->hdr.msg_size);
        g_test_message("waiting on wait %d ", childpid);
        waitpid(childpid,&status,0); /* wait for child to finish up */
        
        /* sleep for sometime to allow the polling thread
           to process the send
        */
        CC_LOG_DEBUG("status is %d", status);
        g_test_message("returned from wait ");


        /* clear the log */
        cc_of_log_clear();
        
        /* delete the socket */
        del_sock_msg.fd = sv[0];
        del_sock_msg.fd_type = SOCKET;
        del_sock_msg.fd_action = DELETE_FD;
        del_sock_msg.poll_events = 0;
        del_sock_msg.pollin_func = NULL;
        del_sock_msg.pollout_func = NULL;

        g_test_message("test - delete socket");

        adp_thr_mgr_add_del_fd(&tdata->tp_data, &del_sock_msg);
        
        temp_liblog = cc_of_log_read();    

        g_test_message("Test health after del test socket");

        g_test_message("test - 2 pipes created; 4 pipe fds");
        g_test_message("test - num_pipes %d", tdata->tp_data.num_pipes);
        g_assert_cmpuint(tdata->tp_data.num_pipes, ==, 4);
        
        g_test_message("test - num_avail_sockfd is 10");
        g_assert_cmpint(adp_thr_mgr_get_num_avail_sockfd(&tdata->tp_data),
                        ==, 10);
    
        g_test_message("test - num fd_entry_p in fd_list is 2");
        regex_one_compint(
            temp_liblog,
            "fd_list has ([0-9]+) entries DELETE_FD",
            1, 2);
        
        g_test_message("test - num_pollfds is 2");
        regex_one_compint(
            temp_liblog,
            "num pollfds is ([0-9]+) after DELETE_FD",
            1, 2);
    }
    close(sv[0]);
    close(sv[1]);
    return;
}

#define SOCK_PATH "test_establish_socket"
GMutex *sock_mutex;
GCond  *sock_cond;
int server_fd, accept_fd, connect_fd;

void
test_socket_listen_process_func(char *tname,
                                adpoll_fd_info_t *data_p,
                                adpoll_send_msg_htbl_info_t
                                *unused_data UNUSED)
{
    struct sockaddr_un remote;
//    int accept_fd;
    int t;
    char str[100];
    char test_str[] = "hello 1..2..3..10..20..30";

    g_test_message("test - %s: thread name sent to callback is thread_tc_3",
                   __FUNCTION__);
    g_assert_cmpstr(tname, ==, "thread_tc_5");

    t = sizeof(remote);
    
    g_test_message("test - %s: accept", __FUNCTION__);    
    accept_fd = accept(data_p->fd, (struct sockaddr *)&remote,
                       (socklen_t *)&t);
    g_assert_cmpint(accept_fd, !=, -1);

    g_test_message("test - %s: send data on the accepted fd",
                   __FUNCTION__);
    sprintf(str, "%s", test_str);
    write(accept_fd, str, 100);
}

gpointer
connect_fd_thread_func(gpointer data_p UNUSED)
{
    int retval, len;
//    int connect_fd;
    struct sockaddr_un remote;
    char str[100];
    char test_str[] = "hello 1..2..3..10..20..30";

    CC_LOG_DEBUG("in %s(%d)", __FUNCTION__, __LINE__);
    connect_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    
    g_assert_cmpint(connect_fd, !=, -1);
    
    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    
    CC_LOG_DEBUG("proceeding to lock mutex for wait %p",
                 sock_mutex);
    g_mutex_lock(sock_mutex);
    g_cond_wait(sock_cond, sock_mutex);

    CC_LOG_DEBUG("got signaled on cond/mutex %p/%p",
                 sock_cond, sock_mutex);

    CC_LOG_DEBUG("before connect");    
    retval = connect(connect_fd, (struct sockaddr *)&remote, len);
    CC_LOG_DEBUG("after connect");
    
    g_assert_cmpint(retval, !=, -1);
    g_mutex_unlock(sock_mutex);

    read(connect_fd, str, 100);
    CC_LOG_DEBUG("read %s from connect fd ", str);
    g_assert_cmpstr(str, ==, test_str);
    return NULL;
}

//tc_5 - exercise listen/accept poll
//     - parent: listen() add/del this fd to polling thr
//     -         with a callback that accepts the socket
//     - child: connect to parent's port

static void
pollthread_tc_5(test_data_t *tdata,
                gconstpointer tudata UNUSED)
{
    adpoll_thr_msg_t add_sock_msg;
    int wr_fd;
    GThread *child_thr = NULL;
//    int server_fd;
    int len, retval;
    struct sockaddr_un local;
    
//    cc_of_debug_toggle(TRUE);    //enable if debugging test code

    sock_mutex = g_mutex_new();
    sock_cond = g_cond_new();
    
    g_mutex_init(sock_mutex);
    g_cond_init(sock_cond);

    child_thr = g_thread_create(connect_fd_thread_func,
                                NULL, TRUE, NULL);
    
    // socket()
    // listen ()
    // add_del_fd this listen fd with listen callback
    // send a test message from the callback
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(server_fd, !=, -1);
    
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCK_PATH);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    retval = bind(server_fd, (struct sockaddr *)&local, len);
    g_assert_cmpint(retval, !=, -1);
    
    retval = listen(server_fd, 2);
    g_assert_cmpint(retval, !=, -1);
    
    add_sock_msg.fd = server_fd;
    add_sock_msg.fd_type = SOCKET;
    add_sock_msg.fd_action = ADD_FD;
    add_sock_msg.poll_events = POLLIN;
    add_sock_msg.pollin_func = &test_socket_listen_process_func;
    add_sock_msg.pollout_func = NULL;
    
    /* clear the log */
    cc_of_log_clear();
    
    wr_fd = adp_thr_mgr_add_del_fd(&tdata->tp_data, &add_sock_msg);
    
    g_assert (wr_fd != -1);
    g_assert_cmpint(wr_fd, ==, server_fd);
    
    CC_LOG_DEBUG("signaling cond/mutex: %p/%p",
                 sock_cond, sock_mutex);
    
    g_mutex_lock(sock_mutex);
    g_cond_signal(sock_cond);
    g_mutex_unlock(sock_mutex);
    
    CC_LOG_DEBUG("signaled and unlocked");
    
    g_thread_join(child_thr);
    
    adp_thr_mgr_free(&(tdata->tp_data));
    g_mutex_free(sock_mutex);
    g_cond_free(sock_cond);
    close(server_fd);
    close(accept_fd);
    close(connect_fd);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add("/pollthread/tc_1",
               test_data_t, /* fixture data - no need to allocate, just give the type */
               "thread_tc_1",                /* user data - second argument to the functions */
               pollthread_start, pollthread_tc_1, pollthread_end);


    g_test_add("/pollthread/tc_2",
               test_data_t,
               "thread_tc_2",
               pollthread_start, pollthread_tc_2, pollthread_end);

    g_test_add("/pollthread/tc_3",
               test_data_t,
               "thread_tc_3",
               pollthread_start, pollthread_tc_3, pollthread_end);

    g_test_add("/pollthread/tc_4",
               test_data_t,
               "thread_tc_4",
               pollthread_start, pollthread_tc_4, pollthread_end);
    
    g_test_add("/pollthread/tc_5",
               test_data_t,
               "thread_tc_5",
               pollthread_start, pollthread_tc_5, NULL);

    return g_test_run();
}
