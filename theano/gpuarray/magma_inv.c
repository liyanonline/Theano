#section init_code

setup_ext_cuda();

#section support_code_struct

int APPLY_SPECIFIC(magma_inv)(PyGpuArrayObject *A, PyGpuArrayObject **_A_inv,
                              PyGpuContextObject *c) {
  PyGpuArrayObject *A_inv = *_A_inv;
  const size_t *dims;
  magma_int_t N, ldwork, info;
  magma_int_t *piv = NULL;
  gpudata *dwork = NULL;
  int res = -1;

  if (A->ga.typecode != GA_FLOAT) {
    PyErr_SetString(PyExc_TypeError,
                    "GpuMagmaMatrixInverse: Unsupported data type");
    return -1;
  }

  // This is early to match the exit() in the fail label.
  cuda_enter(c->ctx);
  magma_init();

  if (!GpuArray_IS_C_CONTIGUOUS(&A->ga)) {
    PyErr_SetString(PyExc_ValueError,
                    "GpuMagmaMatrixInverse: requires data to be C-contiguous");
    return 1;
  }
  if (PyGpuArray_NDIM(A) != 2) {
    PyErr_SetString(PyExc_ValueError,
                    "GpuMagmaMatrixInverse: matrix rank error");
    goto fail;
  }
  dims = PyGpuArray_DIMS(A);
  if (dims[0] != dims[1]) {
    PyErr_SetString(PyExc_ValueError,
                    "GpuMagmaMatrixInverse: matrix is not square");
    goto fail;
  }
#ifdef INPLACE
  Py_XDECREF(out);
  A_inv = A;
  Py_INCREF(out);
#else
  A_inv = theano_try_copy(A_inv, A);
  if (A_inv == NULL) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "GpuMagmaMatrixInverse: failed to allocate memory for the output");
    goto fail;
  }
#endif
  // magma matrix inverse

  N = dims[0];

  ldwork = N * magma_get_sgetri_nb(N);
  dwork = gpudata_alloc(c->ctx, ldwork * sizeof(float), NULL, 0, NULL);
  if (dwork == NULL) {
    PyErr_SetString(PyExc_RuntimeError,
                    "GpuMagmaMatrixInverse: failed to allocate working memory");
    goto fail;
  }

  if (magma_imalloc_cpu(&piv, N)) {
    PyErr_SetString(
        PyExc_RuntimeError,
        "GpuMagmaMatrixInverse: failed to allocate memory for the pivot array");
    goto fail;
  }

  magma_sgetrf_gpu(N, N, (float *)PyGpuArray_DEV_DATA(A_inv), N, piv, &info);
  if (info != 0) {
    PyErr_Format(
        PyExc_RuntimeError,
        "GpuMagmaMatrixInverse: magma_sgetrf_gpu returned error %d: %s.", info,
        magma_strerror(info));
    goto fail;
  }
  magma_sgetri_gpu(N, (float *)PyGpuArray_DEV_DATA(A_inv), N, piv,
                   *(float **)dwork, ldwork, &info);
  if (info != 0) {
    PyErr_Format(
        PyExc_RuntimeError,
        "GpuMagmaMatrixInverse: magma_sgetri_gpu returned error %d: %s.", info,
        magma_strerror(info));
    goto fail;
  }
  res = 0;
  *_A_inv = A_inv;
fail:
  if (piv != NULL)
    magma_free(piv);
  if (dwork != NULL)
    gpudata_release(dwork);
  magma_finalize();
  cuda_exit(c->ctx);
  return res;
}
