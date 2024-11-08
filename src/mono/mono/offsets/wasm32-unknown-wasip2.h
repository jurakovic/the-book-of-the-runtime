#ifndef USED_CROSS_COMPILER_OFFSETS
#ifdef TARGET_WASI
#ifndef HAVE_BOEHM_GC
#define HAS_CROSS_COMPILER_OFFSETS
#if defined (USE_CROSS_COMPILE_OFFSETS) || defined (MONO_CROSS_COMPILE)
#if !defined (DISABLE_METADATA_OFFSETS)
#define USED_CROSS_COMPILER_OFFSETS
DECL_ALIGN2(gint8,1)
DECL_ALIGN2(gint16,2)
DECL_ALIGN2(gint32,4)
DECL_ALIGN2(gint64,8)
DECL_ALIGN2(float,4)
DECL_ALIGN2(double,8)
DECL_ALIGN2(gpointer,4)
DECL_SIZE2(gint8,1)
DECL_SIZE2(gint16,2)
DECL_SIZE2(gint32,4)
DECL_SIZE2(gint64,8)
DECL_SIZE2(float,4)
DECL_SIZE2(double,8)
DECL_SIZE2(gpointer,4)
DECL_SIZE2(MonoObject,8)
DECL_OFFSET2(MonoObject,vtable,0)
DECL_OFFSET2(MonoObject,synchronisation,4)
DECL_SIZE2(MonoClass,144)
DECL_OFFSET2(MonoClass,element_class,0)
DECL_OFFSET2(MonoClass,cast_class,4)
DECL_OFFSET2(MonoClass,supertypes,8)
DECL_OFFSET2(MonoClass,idepth,12)
DECL_OFFSET2(MonoClass,rank,14)
DECL_OFFSET2(MonoClass,class_kind,15)
DECL_OFFSET2(MonoClass,instance_size,16)
DECL_OFFSET2(MonoClass,min_align,22)
DECL_OFFSET2(MonoClass,parent,28)
DECL_OFFSET2(MonoClass,nested_in,32)
DECL_OFFSET2(MonoClass,image,36)
DECL_OFFSET2(MonoClass,name,40)
DECL_OFFSET2(MonoClass,name_space,44)
DECL_OFFSET2(MonoClass,type_token,48)
DECL_OFFSET2(MonoClass,vtable_size,52)
DECL_OFFSET2(MonoClass,interface_count,56)
DECL_OFFSET2(MonoClass,interface_id,60)
DECL_OFFSET2(MonoClass,max_interface_id,64)
DECL_OFFSET2(MonoClass,interface_offsets_count,68)
DECL_OFFSET2(MonoClass,interfaces_packed,72)
DECL_OFFSET2(MonoClass,interface_offsets_packed,76)
DECL_OFFSET2(MonoClass,interface_bitmap,80)
DECL_OFFSET2(MonoClass,inlinearray_value,84)
DECL_OFFSET2(MonoClass,name_hash,88)
DECL_OFFSET2(MonoClass,interfaces,92)
DECL_OFFSET2(MonoClass,sizes,96)
DECL_OFFSET2(MonoClass,fields,100)
DECL_OFFSET2(MonoClass,methods,104)
DECL_OFFSET2(MonoClass,this_arg,108)
DECL_OFFSET2(MonoClass,_byval_arg,116)
DECL_OFFSET2(MonoClass,gc_descr,124)
DECL_OFFSET2(MonoClass,runtime_vtable,128)
DECL_OFFSET2(MonoClass,vtable,132)
DECL_OFFSET2(MonoClass,infrequent_data,136)
DECL_OFFSET2(MonoClass,variant_search_table,140)
DECL_SIZE2(MonoVTable,44)
DECL_OFFSET2(MonoVTable,klass,0)
DECL_OFFSET2(MonoVTable,gc_descr,4)
DECL_OFFSET2(MonoVTable,domain,8)
DECL_OFFSET2(MonoVTable,type,12)
DECL_OFFSET2(MonoVTable,interface_bitmap,16)
DECL_OFFSET2(MonoVTable,loader_alloc,20)
DECL_OFFSET2(MonoVTable,max_interface_id,24)
DECL_OFFSET2(MonoVTable,rank,28)
DECL_OFFSET2(MonoVTable,initialized,29)
DECL_OFFSET2(MonoVTable,flags,30)
DECL_OFFSET2(MonoVTable,imt_collisions_bitmap,32)
DECL_OFFSET2(MonoVTable,runtime_generic_context,36)
DECL_OFFSET2(MonoVTable,ee_data,40)
DECL_OFFSET2(MonoVTable,vtable,44)
DECL_SIZE2(MonoDelegate,60)
DECL_OFFSET2(MonoDelegate,object,0)
DECL_OFFSET2(MonoDelegate,method_ptr,8)
DECL_OFFSET2(MonoDelegate,invoke_impl,12)
DECL_OFFSET2(MonoDelegate,target,16)
DECL_OFFSET2(MonoDelegate,method,20)
DECL_OFFSET2(MonoDelegate,delegate_trampoline,24)
DECL_OFFSET2(MonoDelegate,extra_arg,28)
DECL_OFFSET2(MonoDelegate,invoke_info,32)
DECL_OFFSET2(MonoDelegate,interp_method,36)
DECL_OFFSET2(MonoDelegate,interp_invoke_impl,40)
DECL_OFFSET2(MonoDelegate,method_info,44)
DECL_OFFSET2(MonoDelegate,original_method_info,48)
DECL_OFFSET2(MonoDelegate,data,52)
DECL_OFFSET2(MonoDelegate,method_is_virtual,56)
DECL_OFFSET2(MonoDelegate,bound,57)
DECL_SIZE2(MonoInternalThread,136)
DECL_OFFSET2(MonoInternalThread,obj,0)
DECL_OFFSET2(MonoInternalThread,lock_thread_id,8)
DECL_OFFSET2(MonoInternalThread,handle,12)
DECL_OFFSET2(MonoInternalThread,native_handle,16)
DECL_OFFSET2(MonoInternalThread,name,20)
DECL_OFFSET2(MonoInternalThread,state,32)
DECL_OFFSET2(MonoInternalThread,abort_exc,36)
DECL_OFFSET2(MonoInternalThread,abort_state_handle,40)
DECL_OFFSET2(MonoInternalThread,tid,48)
DECL_OFFSET2(MonoInternalThread,debugger_thread,56)
DECL_OFFSET2(MonoInternalThread,static_data,60)
DECL_OFFSET2(MonoInternalThread,thread_info,64)
DECL_OFFSET2(MonoInternalThread,__interruption_requested,68)
DECL_OFFSET2(MonoInternalThread,longlived,72)
DECL_OFFSET2(MonoInternalThread,threadpool_thread,76)
DECL_OFFSET2(MonoInternalThread,external_eventloop,77)
DECL_OFFSET2(MonoInternalThread,apartment_state,78)
DECL_OFFSET2(MonoInternalThread,managed_id,80)
DECL_OFFSET2(MonoInternalThread,small_id,84)
DECL_OFFSET2(MonoInternalThread,manage_callback,88)
DECL_OFFSET2(MonoInternalThread,flags,92)
DECL_OFFSET2(MonoInternalThread,thread_pinning_ref,96)
DECL_OFFSET2(MonoInternalThread,priority,100)
DECL_OFFSET2(MonoInternalThread,owned_mutexes,104)
DECL_OFFSET2(MonoInternalThread,suspended,108)
DECL_OFFSET2(MonoInternalThread,self_suspended,112)
DECL_OFFSET2(MonoInternalThread,thread_state,116)
DECL_OFFSET2(MonoInternalThread,internal_thread,120)
DECL_OFFSET2(MonoInternalThread,pending_exception,124)
DECL_OFFSET2(MonoInternalThread,last,128)
DECL_SIZE2(MonoMulticastDelegate,64)
DECL_OFFSET2(MonoMulticastDelegate,delegate,0)
DECL_OFFSET2(MonoMulticastDelegate,delegates,60)
DECL_SIZE2(MonoTransparentProxy,20)
DECL_OFFSET2(MonoTransparentProxy,object,0)
DECL_OFFSET2(MonoTransparentProxy,rp,8)
DECL_OFFSET2(MonoTransparentProxy,remote_class,12)
DECL_OFFSET2(MonoTransparentProxy,custom_type_info,16)
DECL_SIZE2(MonoRealProxy,40)
DECL_OFFSET2(MonoRealProxy,object,0)
DECL_OFFSET2(MonoRealProxy,class_to_proxy,8)
DECL_OFFSET2(MonoRealProxy,context,12)
DECL_OFFSET2(MonoRealProxy,unwrapped_server,16)
DECL_OFFSET2(MonoRealProxy,target_domain_id,20)
DECL_OFFSET2(MonoRealProxy,target_uri,24)
DECL_OFFSET2(MonoRealProxy,object_identity,28)
DECL_OFFSET2(MonoRealProxy,obj_TP,32)
DECL_OFFSET2(MonoRealProxy,stub_data,36)
DECL_SIZE2(MonoRemoteClass,20)
DECL_OFFSET2(MonoRemoteClass,default_vtable,0)
DECL_OFFSET2(MonoRemoteClass,xdomain_vtable,4)
DECL_OFFSET2(MonoRemoteClass,proxy_class,8)
DECL_OFFSET2(MonoRemoteClass,proxy_class_name,12)
DECL_OFFSET2(MonoRemoteClass,interface_count,16)
DECL_OFFSET2(MonoRemoteClass,interfaces,20)
DECL_SIZE2(MonoArray,16)
DECL_OFFSET2(MonoArray,obj,0)
DECL_OFFSET2(MonoArray,bounds,8)
DECL_OFFSET2(MonoArray,max_length,12)
DECL_OFFSET2(MonoArray,vector,16)
DECL_SIZE2(MonoArrayBounds,8)
DECL_OFFSET2(MonoArrayBounds,length,0)
DECL_OFFSET2(MonoArrayBounds,lower_bound,4)
DECL_SIZE2(MonoSafeHandle,12)
DECL_OFFSET2(MonoSafeHandle,base,0)
DECL_OFFSET2(MonoSafeHandle,handle,8)
DECL_SIZE2(MonoHandleRef,8)
DECL_OFFSET2(MonoHandleRef,wrapper,0)
DECL_OFFSET2(MonoHandleRef,handle,4)
DECL_SIZE2(MonoString,12)
DECL_OFFSET2(MonoString,object,0)
DECL_OFFSET2(MonoString,length,8)
DECL_OFFSET2(MonoString,chars,12)
DECL_SIZE2(MonoException,72)
DECL_OFFSET2(MonoException,object,0)
DECL_OFFSET2(MonoException,class_name,8)
DECL_OFFSET2(MonoException,message,12)
DECL_OFFSET2(MonoException,_data,16)
DECL_OFFSET2(MonoException,inner_ex,20)
DECL_OFFSET2(MonoException,help_link,24)
DECL_OFFSET2(MonoException,trace_ips,28)
DECL_OFFSET2(MonoException,stack_trace,32)
DECL_OFFSET2(MonoException,remote_stack_trace,36)
DECL_OFFSET2(MonoException,remote_stack_index,40)
DECL_OFFSET2(MonoException,dynamic_methods,44)
DECL_OFFSET2(MonoException,hresult,48)
DECL_OFFSET2(MonoException,source,52)
DECL_OFFSET2(MonoException,serialization_manager,56)
DECL_OFFSET2(MonoException,captured_traces,60)
DECL_OFFSET2(MonoException,native_trace_ips,64)
DECL_OFFSET2(MonoException,caught_in_unmanaged,68)
DECL_SIZE2(MonoTypedRef,12)
DECL_OFFSET2(MonoTypedRef,type,0)
DECL_OFFSET2(MonoTypedRef,value,4)
DECL_OFFSET2(MonoTypedRef,klass,8)
DECL_SIZE2(MonoThreadsSync,32)
DECL_OFFSET2(MonoThreadsSync,status,0)
DECL_OFFSET2(MonoThreadsSync,nest,4)
DECL_OFFSET2(MonoThreadsSync,hash_code,8)
DECL_OFFSET2(MonoThreadsSync,wait_list,12)
DECL_OFFSET2(MonoThreadsSync,data,16)
DECL_OFFSET2(MonoThreadsSync,entry_mutex,20)
DECL_OFFSET2(MonoThreadsSync,entry_cond,24)
DECL_OFFSET2(MonoThreadsSync,is_interned,28)
DECL_SIZE2(SgenThreadInfo,312)
DECL_OFFSET2(SgenThreadInfo,client_info,0)
DECL_OFFSET2(SgenThreadInfo,tlab_start,288)
DECL_OFFSET2(SgenThreadInfo,tlab_next,292)
DECL_OFFSET2(SgenThreadInfo,tlab_temp_end,296)
DECL_OFFSET2(SgenThreadInfo,tlab_real_end,300)
DECL_OFFSET2(SgenThreadInfo,total_bytes_allocated,304)
DECL_SIZE2(SgenClientThreadInfo,288)
DECL_OFFSET2(SgenClientThreadInfo,info,0)
DECL_OFFSET2(SgenClientThreadInfo,skip,240)
DECL_OFFSET2(SgenClientThreadInfo,suspend_done,244)
DECL_OFFSET2(SgenClientThreadInfo,in_critical_region,248)
DECL_OFFSET2(SgenClientThreadInfo,runtime_data,252)
DECL_OFFSET2(SgenClientThreadInfo,stack_end,256)
DECL_OFFSET2(SgenClientThreadInfo,stack_start,260)
DECL_OFFSET2(SgenClientThreadInfo,stack_start_limit,264)
DECL_OFFSET2(SgenClientThreadInfo,ctx,268)
DECL_SIZE2(MonoProfilerCallContext,36)
DECL_OFFSET2(MonoProfilerCallContext,context,0)
DECL_OFFSET2(MonoProfilerCallContext,interp_frame,20)
DECL_OFFSET2(MonoProfilerCallContext,method,24)
DECL_OFFSET2(MonoProfilerCallContext,return_value,28)
DECL_OFFSET2(MonoProfilerCallContext,args,32)
#endif //disable metadata check
#ifndef DISABLE_JIT_OFFSETS
#define USED_CROSS_COMPILER_OFFSETS
DECL_SIZE2(MonoLMF,12)
DECL_OFFSET2(MonoLMF,previous_lmf,0)
DECL_OFFSET2(MonoLMF,lmf_addr,4)
DECL_OFFSET2(MonoLMF,method,8)
DECL_SIZE2(MonoLMFExt,44)
DECL_OFFSET2(MonoLMFExt,lmf,0)
DECL_OFFSET2(MonoLMFExt,kind,12)
DECL_OFFSET2(MonoLMFExt,ctx,16)
DECL_OFFSET2(MonoLMFExt,interp_exit_data,36)
DECL_OFFSET2(MonoLMFExt,il_state,40)
DECL_SIZE2(MonoMethodILState,12)
DECL_OFFSET2(MonoMethodILState,method,0)
DECL_OFFSET2(MonoMethodILState,il_offset,4)
DECL_OFFSET2(MonoMethodILState,data,8)
DECL_SIZE2(MonoMethodRuntimeGenericContext,16)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,class_vtable,0)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,method,4)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,method_inst,8)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,entries,12)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,infos,16)
DECL_SIZE2(MonoJitTlsData,216)
DECL_OFFSET2(MonoJitTlsData,end_of_stack,0)
DECL_OFFSET2(MonoJitTlsData,stack_size,4)
DECL_OFFSET2(MonoJitTlsData,lmf,8)
DECL_OFFSET2(MonoJitTlsData,first_lmf,12)
DECL_OFFSET2(MonoJitTlsData,signal_stack,20)
DECL_OFFSET2(MonoJitTlsData,signal_stack_size,24)
DECL_OFFSET2(MonoJitTlsData,stack_ovf_guard_base,28)
DECL_OFFSET2(MonoJitTlsData,stack_ovf_guard_size,32)
DECL_OFFSET2(MonoJitTlsData,abort_func,40)
DECL_OFFSET2(MonoJitTlsData,class_cast_from,44)
DECL_OFFSET2(MonoJitTlsData,class_cast_to,48)
DECL_OFFSET2(MonoJitTlsData,ex_ctx,52)
DECL_OFFSET2(MonoJitTlsData,resume_state,72)
DECL_OFFSET2(MonoJitTlsData,handler_block,144)
DECL_OFFSET2(MonoJitTlsData,handler_block_context,148)
DECL_OFFSET2(MonoJitTlsData,orig_ex_ctx,168)
DECL_OFFSET2(MonoJitTlsData,orig_ex_ctx_set,188)
DECL_OFFSET2(MonoJitTlsData,thrown_exc,192)
DECL_OFFSET2(MonoJitTlsData,thrown_non_exc,196)
DECL_OFFSET2(MonoJitTlsData,calling_image,200)
DECL_OFFSET2(MonoJitTlsData,abort_exc_stack_threshold,204)
DECL_OFFSET2(MonoJitTlsData,active_jit_methods,208)
DECL_OFFSET2(MonoJitTlsData,interp_context,212)
DECL_SIZE2(MonoGSharedVtMethodRuntimeInfo,4)
DECL_OFFSET2(MonoGSharedVtMethodRuntimeInfo,locals_size,0)
DECL_OFFSET2(MonoGSharedVtMethodRuntimeInfo,entries,4)
DECL_SIZE2(MonoContext,20)
DECL_OFFSET2(MonoContext,wasm_sp,0)
DECL_OFFSET2(MonoContext,wasm_bp,4)
DECL_OFFSET2(MonoContext,llvm_exc_reg,8)
DECL_OFFSET2(MonoContext,wasm_ip,12)
DECL_OFFSET2(MonoContext,wasm_pc,16)
DECL_SIZE2(MonoDelegateTrampInfo,44)
DECL_OFFSET2(MonoDelegateTrampInfo,klass,0)
DECL_OFFSET2(MonoDelegateTrampInfo,invoke,4)
DECL_OFFSET2(MonoDelegateTrampInfo,method,8)
DECL_OFFSET2(MonoDelegateTrampInfo,invoke_sig,12)
DECL_OFFSET2(MonoDelegateTrampInfo,sig,16)
DECL_OFFSET2(MonoDelegateTrampInfo,method_ptr,20)
DECL_OFFSET2(MonoDelegateTrampInfo,invoke_impl,24)
DECL_OFFSET2(MonoDelegateTrampInfo,impl_this,28)
DECL_OFFSET2(MonoDelegateTrampInfo,impl_nothis,32)
DECL_OFFSET2(MonoDelegateTrampInfo,need_rgctx_tramp,36)
DECL_OFFSET2(MonoDelegateTrampInfo,is_virtual,40)
DECL_SIZE2(MonoFtnDesc,16)
DECL_OFFSET2(MonoFtnDesc,addr,0)
DECL_OFFSET2(MonoFtnDesc,arg,4)
DECL_OFFSET2(MonoFtnDesc,method,8)
DECL_OFFSET2(MonoFtnDesc,interp_method,12)
#endif //disable jit check
#endif //cross compiler checks
#endif //gc check
#endif //TARGET_WASI
#endif //USED_CROSS_COMPILER_OFFSETS check
