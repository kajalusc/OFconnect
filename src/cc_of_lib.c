#include "cc_of_global.h"


cc_of_global_t cc_of_global;
// Does this need a lock for the global struct ? 

extern net_svcs_t tcp_sockfns;
extern net_svcs_t udp_sockfns;


inline const char *cc_of_strerror(int errnum)
{
    if(errnum > 0) {
	    return "Invalid errnum";
    } else if (errnum <= -(int)CC_OF_ERRTABLE_SIZE){
	    return "Unknown error";
    } else {
	    return cc_of_errtable[-errnum];
    }
}


gboolean cc_ofdev_htbl_equal_func(gconstpointer a, gconstpointer b)
{
    cc_ofdev_key_t *a_dev, *b_dev;
    a_dev = (cc_ofdev_key_t *)a;
    b_dev = (cc_ofdev_key_t *)b;

    if ((a_dev->controller_ip_addr == b_dev->controller_ip_addr) && 
	(a_dev->switch_ip_addr == b_dev->switch_ip_addr) &&
    (a_dev->controller_L4_port == b_dev->controller_L4_port) &&
	(a_dev->switch_L4_port == b_dev->switch_L4_port) &&
    (a_dev->layer4_proto == b_dev->layer4_proto )) {
	    return TRUE;
    } else {
	    return FALSE;
    }
}

void cc_of_destroy_generic(gpointer data)
{
    g_free(data);
}

void cc_ofdev_htbl_destroy_val(gpointer data)
{
    cc_ofdev_info_t *dev_info_p;
    dev_info_p = (cc_ofdev_info_t *)data;
    g_mutex_lock(&dev_info_p->ofrw_socket_list_lock);
    g_list_free_full(dev_info_p->ofrw_socket_list,
                     cc_of_destroy_generic);
    g_mutex_lock(&dev_info_p->ofrw_socket_list_lock);
    
    g_mutex_clear(&dev_info_p->ofrw_socket_list_lock);
    
    g_free(data);
}


gboolean cc_ofchannel_htbl_equal_func(gconstpointer a, gconstpointer b)
{
    cc_ofchannel_key_t *a_chan, *b_chan;
    a_chan = (cc_ofchannel_key_t *)a;
    b_chan = (cc_ofchannel_key_t *)b;

    if ((a_chan->dp_id == b_chan->dp_id) &&
        (a_chan->aux_id == b_chan->aux_id)) {
         return TRUE;
     } else {
         return FALSE;
    }
}


gboolean cc_ofrw_htbl_equal_func(gconstpointer a, gconstpointer b)
{
    cc_ofrw_key_t *a_rw, *b_rw;
    a_rw = (cc_ofrw_key_t *)a;
    b_rw = (cc_ofrw_key_t *)b;

    if (a_rw->rw_sockfd == b_rw->rw_sockfd) {
	    return TRUE;
    } else {
	    return FALSE;
    }
}


/* Call destroy lib for error cases */
int
cc_of_lib_abort()
{
    CC_LOG_ERROR("%s NOT IMPLEMENTED", __FUNCTION__);
    return CC_OF_OK;
}


int
cc_of_lib_init(of_dev_type_e dev_type, of_drv_type_e drv_type)
{
    cc_of_ret status = CC_OF_OK;

    // Initialize cc_of_global
    cc_of_global.ofdrv_type = drv_type;
    cc_of_global.ofdev_type = dev_type;

    cc_of_global.ofdev_htbl = g_hash_table_new_full(g_direct_hash,
                                                    cc_ofdev_htbl_equal_func,
                                                    cc_of_destroy_generic,
                                                    cc_ofdev_htbl_destroy_val);
    if (cc_of_global.ofdev_htbl == NULL) {
	    status = CC_OF_EHTBL;
	    cc_of_lib_abort();
	    CC_LOG_FATAL("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
    }
    g_mutex_init(&cc_of_global.ofdev_htbl_lock);

    cc_of_global.ofchannel_htbl = g_hash_table_new_full(g_direct_hash,
                                                        cc_ofchannel_htbl_equal_func,
                                                        cc_of_destroy_generic,
                                                        cc_of_destroy_generic);
    if (cc_of_global.ofdev_htbl == NULL) {
	    status = CC_OF_EHTBL;
	    cc_of_lib_abort();
	    CC_LOG_FATAL("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
    }
    g_mutex_init(&cc_of_global.ofchannel_htbl_lock);

    cc_of_global.ofrw_htbl = g_hash_table_new_full(g_direct_hash,
                                                   cc_ofrw_htbl_equal_func,
                                                   cc_of_destroy_generic,
                                                   cc_of_destroy_generic);

    if (cc_of_global.ofdev_htbl == NULL) {
	    status = CC_OF_EHTBL;
	    cc_of_lib_abort();
	    CC_LOG_FATAL("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
    }
    g_mutex_init(&cc_of_global.ofrw_htbl_lock);

    cc_of_global.NET_SVCS[TCP] = tcp_sockfns;
    cc_of_global.NET_SVCS[UDP] = udp_sockfns;

    cc_of_global.oflisten_pollthr_p = adp_thr_mgr_new("oflisten_thr",
                                                      MAX_PER_THREAD_RWSOCKETS,
                                                      MAX_PIPE_PER_THR_MGR);
    if (cc_of_global.oflisten_pollthr_p == NULL) {
	    status = CC_OF_EMISC;
	    cc_of_lib_abort();
	    CC_LOG_FATAL("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
    }
    cc_of_global.ofrw_pollthr_list = NULL;

    if ((status = cc_create_rw_pollthr(NULL, 
                                       MAX_PER_THREAD_RWSOCKETS, 
                                       MAX_PIPE_PER_THR_MGR)) < 0) {
	    cc_of_lib_abort();
	    CC_LOG_FATAL("%s(%d): %s", __FUNCTION__, __LINE__, 
                     cc_of_strerror(status));
    }
    
    CC_LOG_INFO("%s(%d): %s", __FUNCTION__, __LINE__,
                "CC_OF_Library initilaized successfully");
    return status;
}


static void process_listenfd_pollin_func(char *tname UNUSED, void *data) {
    
    adpoll_fd_info_t *data_p = (adpoll_fd_info_t *)data;
    int listenfd;    

    if (!data_p) {
        CC_LOG_INFO("%s(%d): %s", __FUNCTION__, __LINE__,
                    "Invalid data passed to listenfd callback."
                    "Cannot accept new connection.");
        return;
    }
    
    listenfd = data_p->fd;

    /* 
     * Do a reverse lookup to get the dev_key 
     * corresponding to this listenfd
     *
     */
    GHashTableIter ofdev_iter;
    cc_ofdev_key_t *dev_key;
    cc_ofdev_info_t *dev_info;
    g_hash_table_iter_init(&ofdev_iter, cc_of_global.ofdev_htbl);
    if (g_hash_table_iter_next(&ofdev_iter, (gpointer *)&dev_key, (gpointer *)&dev_info)) {
        if (dev_info->main_sockfd == listenfd) {
            // Call NetSVCS Accept
            cc_of_global.NET_SVCS[dev_key->layer4_proto].accept_conn(listenfd);
        }
    }

}


// TODO: switch case for switch
int cc_of_dev_register(cc_ofdev_key_t dev_key, cc_ofver_e max_ofver, 
                       cc_of_recv_pkt recv_func) {
    cc_of_ret status = CC_OF_OK;
    cc_ofdev_key_t *key;
    cc_ofdev_info_t *dev_info;
    char switch_ip[INET_ADDRSTRLEN];
    char controller_ip[INET_ADDRSTRLEN]; 

   
    key = g_malloc0(sizeof(cc_ofdev_key_t));
    if (key == NULL) {
	    status = CC_OF_ENOMEM;
	    CC_LOG_ERROR("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
	    return status;
    }

    key->controller_ip_addr = dev_key.controller_ip_addr;
    key->switch_ip_addr = dev_key.switch_ip_addr;
    key->controller_L4_port = dev_key.controller_L4_port;
    key->switch_L4_port = dev_key.switch_L4_port;
    key->layer4_proto = dev_key.layer4_proto;

    inet_ntop(AF_INET, &key->switch_ip_addr, switch_ip, sizeof(switch_ip));
    inet_ntop(AF_INET, &key->controller_ip_addr, controller_ip,
              sizeof(controller_ip));
    
    dev_info = g_malloc0(sizeof(cc_ofdev_info_t));
    if (dev_info == NULL) {
	    status = CC_OF_ENOMEM;
	    CC_LOG_ERROR("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
	    return status;
    }

    dev_info->of_max_ver = max_ofver;
    dev_info->ofrw_socket_list = NULL;
    g_mutex_init(&dev_info->ofrw_socket_list_lock);

    dev_info->recv_func = recv_func;

    dev_info->main_sockfd =
        cc_of_global.NET_SVCS[key->layer4_proto].open_serverfd(*key);
    if (dev_info->main_sockfd < 0) {
	    status = CC_OF_EMISC;
	    CC_LOG_ERROR("%s(%d): %s", __FUNCTION__, __LINE__,
                     cc_of_strerror(status));
	    return status;
    }

    // Add listenfd to the global oflisten_pollthr
    adpoll_thr_msg_t thr_msg;

    thr_msg.fd = dev_info->main_sockfd;
    thr_msg.fd_type = SOCKET;
    thr_msg.fd_action = ADD;
    thr_msg.poll_events = POLLIN;
    thr_msg.pollin_func = &process_listenfd_pollin_func;
    thr_msg.pollin_user_data = NULL; 
    thr_msg.pollout_func = NULL;
    thr_msg.pollout_user_data = NULL;

    adp_thr_mgr_add_del_fd(cc_of_global.oflisten_pollthr_p, &thr_msg);
    CC_LOG_INFO("%s(%d): %s ,controllerIP:%s switchIP:%s,"
                "layer4Prot:%d", __FUNCTION__, __LINE__, 
                "CC_OF_DEV initilaized successfully", 
                controller_ip, switch_ip, key->layer4_proto);

    // Add this new device entry to ofdev_htbl
    g_hash_table_insert(cc_of_global.ofdev_htbl, (gpointer)key, 
                        (gpointer)dev_info);

    return status;
}


