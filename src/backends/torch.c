#include "backends/torch.h"
#include "tensor.h"
#include "model.h"
#include "script.h"
#include "utils/alloc.h"
#include "utils/arr_rm_alloc.h"
#include "torch_c.h"


RAI_Model *RAI_ModelCreateTorch(RAI_Backend backend, RAI_Device device,
                                const char *modeldef, size_t modellen,
                                RAI_Error *error) {
  DLDeviceType dl_device;
  switch (device) {
    case RAI_DEVICE_CPU:
      dl_device = kDLCPU;
      break;
    case RAI_DEVICE_GPU:
      dl_device = kDLGPU;
      break;
    default:
      RAI_SetError(error, RAI_EMODELCONFIGURE, "Error configuring model: unsupported device\n");
      return NULL;
  }

  char* error_descr = NULL;
  void* model = torchLoadModel(modeldef, modellen, dl_device, &error_descr);

  if (model == NULL) {
    RAI_SetError(error, RAI_EMODELCREATE, error_descr);
    free(error_descr);
    return NULL;
  }

  RAI_Model* ret = RedisModule_Calloc(1, sizeof(*ret));
  ret->model = model;
  ret->session = NULL;
  ret->backend = backend;
  ret->device = device;
  ret->inputs = NULL;
  ret->outputs = NULL;
  ret->refCount = 1;

  return ret;
}

void RAI_ModelFreeTorch(RAI_Model* model, RAI_Error *error) {
  torchDeallocContext(model->model);
}

int RAI_ModelRunTorch(RAI_ModelRunCtx* mctx, RAI_Error *error) {

  long ninputs = array_len(mctx->inputs);
  long noutputs;

  DLManagedTensor** inputs = RedisModule_Calloc(ninputs, sizeof(*inputs));
  DLManagedTensor** outputs = NULL;

  for (size_t i=0 ; i<ninputs; ++i) {
    inputs[i] = &mctx->inputs[i].tensor->tensor;
  }

  char* error_descr = NULL;
  torchRunModel(mctx->model->model,
                ninputs, inputs,
                &noutputs, &outputs, &error_descr);

  if (error_descr != NULL) {
    RAI_SetError(error, RAI_EMODELRUN, error_descr);
    free(error_descr);
    return 1;
  }

  int output_keys = array_len(mctx->outputs);

  if (output_keys && array_len(mctx->outputs) != noutputs) {
    RAI_SetError(error, RAI_EMODELRUN, "Number of outputs doesn't correspond to specified output keys");
    return 1;
  }

  for(size_t i=0 ; i<noutputs ; ++i) {
    if (!output_keys) {
      RAI_ModelRunCtxAddOutput(mctx, NULL);
    }
    RAI_Tensor* output_tensor = RAI_TensorCreateFromDLTensor(outputs[i]);
    mctx->outputs[i].tensor = RAI_TensorGetShallowCopy(output_tensor);
  }

  torchFreeOutputs(&outputs);

  return 0;
}

int RAI_ModelSerializeTorch(RAI_Model *model, char **buffer, size_t *len, RAI_Error *error) {
  char* error_descr = NULL;
  torchSerializeModel(model->model, buffer, len, &error_descr);

  if (*buffer == NULL) {
    RAI_SetError(error, RAI_EMODELSERIALIZE, error_descr);
    free(error_descr);
    return 1;
  }

  return 0;
}

RAI_Script *RAI_ScriptCreateTorch(RAI_Device device, const char *scriptdef, RAI_Error *error) {
  DLDeviceType dl_device;
  switch (device) {
    case RAI_DEVICE_CPU:
      dl_device = kDLCPU;
      break;
    case RAI_DEVICE_GPU:
      dl_device = kDLGPU;
      break;
    default:
      RAI_SetError(error, RAI_ESCRIPTCONFIGURE, "Error configuring script: unsupported device\n");
      break;
  }

  char* error_descr = NULL;
  void* script = torchCompileScript(scriptdef, dl_device, &error_descr);

  if (script == NULL) {
    RAI_SetError(error, RAI_ESCRIPTCREATE, error_descr);
    free(error_descr);
    return NULL;
  }

  RAI_Script* ret = RedisModule_Calloc(1, sizeof(*ret));
  ret->script = script;
  ret->scriptdef = RedisModule_Strdup(scriptdef);
  ret->device = device;
  ret->refCount = 1;

  return ret;
}

void RAI_ScriptFreeTorch(RAI_Script* script, RAI_Error* error) {

  torchDeallocContext(script->script);
  RedisModule_Free(script->scriptdef);
  RedisModule_Free(script);
}

int RAI_ScriptRunTorch(RAI_ScriptRunCtx* sctx, RAI_Error* error) {

  long ninputs = array_len(sctx->inputs);
  long noutputs;

  DLManagedTensor* inputs[ninputs];
  DLManagedTensor** outputs = NULL;

  for (size_t i=0; i<ninputs; i++) {
    inputs[i] = &sctx->inputs[i].tensor->tensor;
  }

  char* error_descr = NULL;
  torchRunScript(sctx->script->script, sctx->fnname, ninputs, inputs, &noutputs, &outputs, &error_descr);

  if (error_descr) {
    printf("F\n");
    RAI_SetError(error, RAI_ESCRIPTRUN, error_descr);
    free(error_descr);
    return 1;
  }

  int output_keys = array_len(sctx->outputs);

  if (output_keys && array_len(sctx->outputs) != noutputs) {
    RAI_SetError(error, RAI_EMODELRUN, "Number of outputs doesn't correspond to specified output keys");
    return 1;
  }

  for(size_t i=0 ; i<noutputs ; ++i) {
    if (!output_keys) {
      RAI_ScriptRunCtxAddOutput(sctx);
    }
    RAI_Tensor* output_tensor = RAI_TensorCreateFromDLTensor(outputs[i]);
    sctx->outputs[i].tensor = RAI_TensorGetShallowCopy(output_tensor);
  }

  torchFreeOutputs(&outputs);

  return 0;
}
