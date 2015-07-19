#include <stdio.h>
#include <stdlib.h>
#include <Python.h>
#include <rs.h>

/* SCPConnection type. */
typedef struct {
  PyObject_HEAD
  rs_conn_t *connection;
} SCPConnection;

static PyObject* SCPConnection_new(PyTypeObject *type, PyObject *args,
                                   PyObject *kwds)
{
  // Create a new SCPConnection
  SCPConnection *self;
  self = (SCPConnection *)type->tp_alloc(type, 0);
  if(self != NULL)
  {
    self->connection = NULL;
  }

  return (PyObject *) self;
}

static int SCPConnection_init(SCPConnection *self, PyObject *args,
                              PyObject *kwds)
{
  // Parse the arguments and keyword arguments
  const char *hostname = NULL;  // Host to communicate with
  unsigned int port = 17893;  // Port to communicate on
  double timeout = 0.5;  // Duration to timeout after
  unsigned int n_tries = 5;  // Number of retries before transmitting
  unsigned int window_size = 8;  // Number of allowable outstanding packets

  static char *kwlist[] = {"hostname", "port", "timeout", "n_tries",
                           "window_size", NULL};

  if(!PyArg_ParseTupleAndKeywords(args, kwds, "s|IdII", kwlist, &hostname,
                                  &port, &timeout, &n_tries, &window_size))
    return -1;

  // Convert the port integer to a string
  char port_str[5];  // 16 bits -> 5 characters max (65536)
  sprintf(port_str, "%5d", port);

  // Determine the correct IP address for the hostname
  struct addrinfo *addrinfo;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  if(getaddrinfo(hostname, "17893", &hints, &addrinfo) != 0)
    return -1;

  // Create the connection object for later actions

  return 0;
}

static PyTypeObject SCPConnectionType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "rig_scp.SCPConnection",  /* tp_name */
  sizeof(SCPConnection),  /* tp_basicsize */
  0,  /* tp_itemsize */
  0,  /* tp_dealloc */
  0,  /* tp_print */
  0,  /* tp_getattr */
  0,  /* tp_setattr */
  0,  /* tp_reserved */
  0,  /* tp_repr */
  0,  /* tp_as_number */
  0,  /* tp_as_sequence */
  0,  /* tp_as_mapping */
  0,  /* tp_hash  */
  0,  /* tp_call */
  0,  /* tp_str */
  0,  /* tp_getattro */
  0,  /* tp_setattro */
  0,  /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /* tp_flags */
  "SCP connection to SpiNNaker board",  /* tp_doc */
  0,  /* tp_traverse */
  0,  /* tp_clear */
  0,  /* tp_richcompare */
  0,  /* tp_weaklistoffset */
  0,  /* tp_iter */
  0,  /* tp_iternext */
  0,  /* tp_methods */
  0,  /* tp_members */
  0,  /* tp_getset */
  0,  /* tp_base */
  0,  /* tp_dict */
  0,  /* tp_descr_get */
  0,  /* tp_descr_set */
  0,  /* tp_dictoffset */
  (initproc) SCPConnection_init,  /* tp_init */
  0,  /* tp_alloc */
  SCPConnection_new,  /* tp_new */
};

// List of methods exported
static PyMethodDef RigSCPMethods[] = {
  {NULL, NULL, 0, NULL}  // Sentinel
};

#if PY_MAJOR_VERSION < 3
/* Python 2.x specific code */

PyMODINIT_FUNC initrig_scp(void)
{
  return Py_InitModule("rig_scp", RigSCPMethods);
}
#endif


#if PY_MAJOR_VERSION >= 3
/* Python 3.x specific code */

static struct PyModuleDef rigscpmodule = {
  PyModuleDef_HEAD_INIT,
  "rig_scp",
  NULL, // TODO Module documentation
  -1, // Size of per-interpreter state of the module
      // (-1 meaning no state/global state).
  RigSCPMethods
};

PyMODINIT_FUNC PyInit_rig_scp(void)
{
  PyObject* module;

  // Create the SCPConnection type
  if(PyType_Ready(&SCPConnectionType) < 0)
    return NULL;

  // Create the module
  module = PyModule_Create(&rigscpmodule);
  if(module == NULL)
    return NULL;

  // Add SCPConnection type to the module
  Py_INCREF(&SCPConnectionType);
  PyModule_AddObject(module, "SCPConnection",
                     (PyObject *)&SCPConnectionType);

  return module;
}
#endif
