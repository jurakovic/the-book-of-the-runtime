#ifndef USED_CROSS_COMPILER_OFFSETS
#ifdef TARGET_AMD64
#ifdef TARGET_MACCAT
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
DECL_ALIGN2(gpointer,8)
DECL_SIZE2(gint8,1)
DECL_SIZE2(gint16,2)
DECL_SIZE2(gint32,4)
DECL_SIZE2(gint64,8)
DECL_SIZE2(float,4)
DECL_SIZE2(double,8)
DECL_SIZE2(gpointer,8)
DECL_SIZE2(MonoObject,16)
DECL_OFFSET2(MonoObject,vtable,0)
DECL_OFFSET2(MonoObject,synchronisation,8)
DECL_SIZE2(MonoClass,240)
DECL_OFFSET2(MonoClass,element_class,0)
DECL_OFFSET2(MonoClass,cast_class,8)
DECL_OFFSET2(MonoClass,supertypes,16)
DECL_OFFSET2(MonoClass,idepth,24)
DECL_OFFSET2(MonoClass,rank,26)
DECL_OFFSET2(MonoClass,class_kind,27)
DECL_OFFSET2(MonoClass,instance_size,28)
DECL_OFFSET2(MonoClass,min_align,34)
DECL_OFFSET2(MonoClass,parent,40)
DECL_OFFSET2(MonoClass,nested_in,48)
DECL_OFFSET2(MonoClass,image,56)
DECL_OFFSET2(MonoClass,name,64)
DECL_OFFSET2(MonoClass,name_space,72)
DECL_OFFSET2(MonoClass,type_token,80)
DECL_OFFSET2(MonoClass,vtable_size,84)
DECL_OFFSET2(MonoClass,interface_count,88)
DECL_OFFSET2(MonoClass,interface_id,92)
DECL_OFFSET2(MonoClass,max_interface_id,96)
DECL_OFFSET2(MonoClass,interface_offsets_count,100)
DECL_OFFSET2(MonoClass,interfaces_packed,104)
DECL_OFFSET2(MonoClass,interface_offsets_packed,112)
DECL_OFFSET2(MonoClass,interface_bitmap,120)
DECL_OFFSET2(MonoClass,inlinearray_value,128)
DECL_OFFSET2(MonoClass,name_hash,132)
DECL_OFFSET2(MonoClass,interfaces,136)
DECL_OFFSET2(MonoClass,sizes,144)
DECL_OFFSET2(MonoClass,fields,152)
DECL_OFFSET2(MonoClass,methods,160)
DECL_OFFSET2(MonoClass,this_arg,168)
DECL_OFFSET2(MonoClass,_byval_arg,184)
DECL_OFFSET2(MonoClass,gc_descr,200)
DECL_OFFSET2(MonoClass,runtime_vtable,208)
DECL_OFFSET2(MonoClass,vtable,216)
DECL_OFFSET2(MonoClass,infrequent_data,224)
DECL_OFFSET2(MonoClass,variant_search_table,232)
DECL_SIZE2(MonoVTable,80)
DECL_OFFSET2(MonoVTable,klass,0)
DECL_OFFSET2(MonoVTable,gc_descr,8)
DECL_OFFSET2(MonoVTable,domain,16)
DECL_OFFSET2(MonoVTable,type,24)
DECL_OFFSET2(MonoVTable,interface_bitmap,32)
DECL_OFFSET2(MonoVTable,loader_alloc,40)
DECL_OFFSET2(MonoVTable,max_interface_id,48)
DECL_OFFSET2(MonoVTable,rank,52)
DECL_OFFSET2(MonoVTable,initialized,53)
DECL_OFFSET2(MonoVTable,flags,54)
DECL_OFFSET2(MonoVTable,imt_collisions_bitmap,56)
DECL_OFFSET2(MonoVTable,runtime_generic_context,64)
DECL_OFFSET2(MonoVTable,ee_data,72)
DECL_OFFSET2(MonoVTable,vtable,80)
DECL_SIZE2(MonoDelegate,120)
DECL_OFFSET2(MonoDelegate,object,0)
DECL_OFFSET2(MonoDelegate,method_ptr,16)
DECL_OFFSET2(MonoDelegate,invoke_impl,24)
DECL_OFFSET2(MonoDelegate,target,32)
DECL_OFFSET2(MonoDelegate,method,40)
DECL_OFFSET2(MonoDelegate,delegate_trampoline,48)
DECL_OFFSET2(MonoDelegate,extra_arg,56)
DECL_OFFSET2(MonoDelegate,invoke_info,64)
DECL_OFFSET2(MonoDelegate,interp_method,72)
DECL_OFFSET2(MonoDelegate,interp_invoke_impl,80)
DECL_OFFSET2(MonoDelegate,method_info,88)
DECL_OFFSET2(MonoDelegate,original_method_info,96)
DECL_OFFSET2(MonoDelegate,data,104)
DECL_OFFSET2(MonoDelegate,method_is_virtual,112)
DECL_OFFSET2(MonoDelegate,bound,113)
DECL_SIZE2(MonoInternalThread,232)
DECL_OFFSET2(MonoInternalThread,obj,0)
DECL_OFFSET2(MonoInternalThread,lock_thread_id,16)
DECL_OFFSET2(MonoInternalThread,handle,24)
DECL_OFFSET2(MonoInternalThread,native_handle,32)
DECL_OFFSET2(MonoInternalThread,name,40)
DECL_OFFSET2(MonoInternalThread,state,56)
DECL_OFFSET2(MonoInternalThread,abort_exc,64)
DECL_OFFSET2(MonoInternalThread,abort_state_handle,72)
DECL_OFFSET2(MonoInternalThread,tid,80)
DECL_OFFSET2(MonoInternalThread,debugger_thread,88)
DECL_OFFSET2(MonoInternalThread,static_data,96)
DECL_OFFSET2(MonoInternalThread,thread_info,104)
DECL_OFFSET2(MonoInternalThread,__interruption_requested,112)
DECL_OFFSET2(MonoInternalThread,longlived,120)
DECL_OFFSET2(MonoInternalThread,threadpool_thread,128)
DECL_OFFSET2(MonoInternalThread,external_eventloop,129)
DECL_OFFSET2(MonoInternalThread,apartment_state,130)
DECL_OFFSET2(MonoInternalThread,managed_id,132)
DECL_OFFSET2(MonoInternalThread,small_id,136)
DECL_OFFSET2(MonoInternalThread,manage_callback,144)
DECL_OFFSET2(MonoInternalThread,flags,152)
DECL_OFFSET2(MonoInternalThread,thread_pinning_ref,160)
DECL_OFFSET2(MonoInternalThread,priority,168)
DECL_OFFSET2(MonoInternalThread,owned_mutexes,176)
DECL_OFFSET2(MonoInternalThread,suspended,184)
DECL_OFFSET2(MonoInternalThread,self_suspended,192)
DECL_OFFSET2(MonoInternalThread,thread_state,200)
DECL_OFFSET2(MonoInternalThread,internal_thread,208)
DECL_OFFSET2(MonoInternalThread,pending_exception,216)
DECL_OFFSET2(MonoInternalThread,last,224)
DECL_SIZE2(MonoMulticastDelegate,128)
DECL_OFFSET2(MonoMulticastDelegate,delegate,0)
DECL_OFFSET2(MonoMulticastDelegate,delegates,120)
DECL_SIZE2(MonoTransparentProxy,40)
DECL_OFFSET2(MonoTransparentProxy,object,0)
DECL_OFFSET2(MonoTransparentProxy,rp,16)
DECL_OFFSET2(MonoTransparentProxy,remote_class,24)
DECL_OFFSET2(MonoTransparentProxy,custom_type_info,32)
DECL_SIZE2(MonoRealProxy,80)
DECL_OFFSET2(MonoRealProxy,object,0)
DECL_OFFSET2(MonoRealProxy,class_to_proxy,16)
DECL_OFFSET2(MonoRealProxy,context,24)
DECL_OFFSET2(MonoRealProxy,unwrapped_server,32)
DECL_OFFSET2(MonoRealProxy,target_domain_id,40)
DECL_OFFSET2(MonoRealProxy,target_uri,48)
DECL_OFFSET2(MonoRealProxy,object_identity,56)
DECL_OFFSET2(MonoRealProxy,obj_TP,64)
DECL_OFFSET2(MonoRealProxy,stub_data,72)
DECL_SIZE2(MonoRemoteClass,40)
DECL_OFFSET2(MonoRemoteClass,default_vtable,0)
DECL_OFFSET2(MonoRemoteClass,xdomain_vtable,8)
DECL_OFFSET2(MonoRemoteClass,proxy_class,16)
DECL_OFFSET2(MonoRemoteClass,proxy_class_name,24)
DECL_OFFSET2(MonoRemoteClass,interface_count,32)
DECL_OFFSET2(MonoRemoteClass,interfaces,40)
DECL_SIZE2(MonoArray,32)
DECL_OFFSET2(MonoArray,obj,0)
DECL_OFFSET2(MonoArray,bounds,16)
DECL_OFFSET2(MonoArray,max_length,24)
DECL_OFFSET2(MonoArray,vector,32)
DECL_SIZE2(MonoArrayBounds,8)
DECL_OFFSET2(MonoArrayBounds,length,0)
DECL_OFFSET2(MonoArrayBounds,lower_bound,4)
DECL_SIZE2(MonoSafeHandle,24)
DECL_OFFSET2(MonoSafeHandle,base,0)
DECL_OFFSET2(MonoSafeHandle,handle,16)
DECL_SIZE2(MonoHandleRef,16)
DECL_OFFSET2(MonoHandleRef,wrapper,0)
DECL_OFFSET2(MonoHandleRef,handle,8)
DECL_SIZE2(MonoString,24)
DECL_OFFSET2(MonoString,object,0)
DECL_OFFSET2(MonoString,length,16)
DECL_OFFSET2(MonoString,chars,20)
DECL_SIZE2(MonoException,144)
DECL_OFFSET2(MonoException,object,0)
DECL_OFFSET2(MonoException,class_name,16)
DECL_OFFSET2(MonoException,message,24)
DECL_OFFSET2(MonoException,_data,32)
DECL_OFFSET2(MonoException,inner_ex,40)
DECL_OFFSET2(MonoException,help_link,48)
DECL_OFFSET2(MonoException,trace_ips,56)
DECL_OFFSET2(MonoException,stack_trace,64)
DECL_OFFSET2(MonoException,remote_stack_trace,72)
DECL_OFFSET2(MonoException,remote_stack_index,80)
DECL_OFFSET2(MonoException,dynamic_methods,88)
DECL_OFFSET2(MonoException,hresult,96)
DECL_OFFSET2(MonoException,source,104)
DECL_OFFSET2(MonoException,serialization_manager,112)
DECL_OFFSET2(MonoException,captured_traces,120)
DECL_OFFSET2(MonoException,native_trace_ips,128)
DECL_OFFSET2(MonoException,caught_in_unmanaged,136)
DECL_SIZE2(MonoTypedRef,24)
DECL_OFFSET2(MonoTypedRef,type,0)
DECL_OFFSET2(MonoTypedRef,value,8)
DECL_OFFSET2(MonoTypedRef,klass,16)
DECL_SIZE2(MonoThreadsSync,48)
DECL_OFFSET2(MonoThreadsSync,status,0)
DECL_OFFSET2(MonoThreadsSync,nest,4)
DECL_OFFSET2(MonoThreadsSync,hash_code,8)
DECL_OFFSET2(MonoThreadsSync,wait_list,16)
DECL_OFFSET2(MonoThreadsSync,data,24)
DECL_OFFSET2(MonoThreadsSync,entry_mutex,32)
DECL_OFFSET2(MonoThreadsSync,entry_cond,40)
DECL_SIZE2(SgenThreadInfo,1600)
DECL_OFFSET2(SgenThreadInfo,client_info,0)
DECL_OFFSET2(SgenThreadInfo,tlab_start,1552)
DECL_OFFSET2(SgenThreadInfo,tlab_next,1560)
DECL_OFFSET2(SgenThreadInfo,tlab_temp_end,1568)
DECL_OFFSET2(SgenThreadInfo,tlab_real_end,1576)
DECL_OFFSET2(SgenThreadInfo,total_bytes_allocated,1584)
DECL_SIZE2(SgenClientThreadInfo,1552)
DECL_OFFSET2(SgenClientThreadInfo,info,0)
DECL_OFFSET2(SgenClientThreadInfo,skip,1104)
DECL_OFFSET2(SgenClientThreadInfo,suspend_done,1108)
DECL_OFFSET2(SgenClientThreadInfo,in_critical_region,1112)
DECL_OFFSET2(SgenClientThreadInfo,runtime_data,1120)
DECL_OFFSET2(SgenClientThreadInfo,stack_end,1128)
DECL_OFFSET2(SgenClientThreadInfo,stack_start,1136)
DECL_OFFSET2(SgenClientThreadInfo,stack_start_limit,1144)
DECL_OFFSET2(SgenClientThreadInfo,ctx,1152)
DECL_SIZE2(MonoProfilerCallContext,432)
DECL_OFFSET2(MonoProfilerCallContext,context,0)
DECL_OFFSET2(MonoProfilerCallContext,interp_frame,400)
DECL_OFFSET2(MonoProfilerCallContext,method,408)
DECL_OFFSET2(MonoProfilerCallContext,return_value,416)
DECL_OFFSET2(MonoProfilerCallContext,args,424)
#endif //disable metadata check
#ifndef DISABLE_JIT_OFFSETS
#define USED_CROSS_COMPILER_OFFSETS
DECL_SIZE2(MonoLMF,24)
DECL_OFFSET2(MonoLMF,previous_lmf,0)
DECL_OFFSET2(MonoLMF,rbp,8)
DECL_OFFSET2(MonoLMF,rsp,16)
DECL_SIZE2(MonoLMFExt,448)
DECL_OFFSET2(MonoLMFExt,lmf,0)
DECL_OFFSET2(MonoLMFExt,kind,24)
DECL_OFFSET2(MonoLMFExt,ctx,32)
DECL_OFFSET2(MonoLMFExt,interp_exit_data,432)
DECL_OFFSET2(MonoLMFExt,il_state,440)
DECL_SIZE2(MonoMethodILState,24)
DECL_OFFSET2(MonoMethodILState,method,0)
DECL_OFFSET2(MonoMethodILState,il_offset,8)
DECL_OFFSET2(MonoMethodILState,data,16)
DECL_SIZE2(MonoMethodRuntimeGenericContext,32)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,class_vtable,0)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,method,8)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,method_inst,16)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,entries,24)
DECL_OFFSET2(MonoMethodRuntimeGenericContext,infos,32)
DECL_SIZE2(MonoJitTlsData,2240)
DECL_OFFSET2(MonoJitTlsData,end_of_stack,0)
DECL_OFFSET2(MonoJitTlsData,stack_size,8)
DECL_OFFSET2(MonoJitTlsData,lmf,16)
DECL_OFFSET2(MonoJitTlsData,first_lmf,24)
DECL_OFFSET2(MonoJitTlsData,signal_stack,40)
DECL_OFFSET2(MonoJitTlsData,signal_stack_size,48)
DECL_OFFSET2(MonoJitTlsData,stack_ovf_guard_base,56)
DECL_OFFSET2(MonoJitTlsData,stack_ovf_guard_size,64)
DECL_OFFSET2(MonoJitTlsData,abort_func,72)
DECL_OFFSET2(MonoJitTlsData,class_cast_from,80)
DECL_OFFSET2(MonoJitTlsData,class_cast_to,88)
DECL_OFFSET2(MonoJitTlsData,ex_ctx,96)
DECL_OFFSET2(MonoJitTlsData,resume_state,496)
DECL_OFFSET2(MonoJitTlsData,handler_block,1360)
DECL_OFFSET2(MonoJitTlsData,handler_block_context,1376)
DECL_OFFSET2(MonoJitTlsData,orig_ex_ctx,1776)
DECL_OFFSET2(MonoJitTlsData,orig_ex_ctx_set,2176)
DECL_OFFSET2(MonoJitTlsData,thrown_exc,2184)
DECL_OFFSET2(MonoJitTlsData,thrown_non_exc,2192)
DECL_OFFSET2(MonoJitTlsData,calling_image,2200)
DECL_OFFSET2(MonoJitTlsData,abort_exc_stack_threshold,2208)
DECL_OFFSET2(MonoJitTlsData,active_jit_methods,2216)
DECL_OFFSET2(MonoJitTlsData,interp_context,2224)
DECL_SIZE2(MonoGSharedVtMethodRuntimeInfo,8)
DECL_OFFSET2(MonoGSharedVtMethodRuntimeInfo,locals_size,0)
DECL_OFFSET2(MonoGSharedVtMethodRuntimeInfo,entries,8)
DECL_SIZE2(MonoContext,400)
DECL_OFFSET2(MonoContext,gregs,0)
DECL_OFFSET2(MonoContext,fregs,144)
DECL_SIZE2(MonoDelegateTrampInfo,80)
DECL_OFFSET2(MonoDelegateTrampInfo,klass,0)
DECL_OFFSET2(MonoDelegateTrampInfo,invoke,8)
DECL_OFFSET2(MonoDelegateTrampInfo,method,16)
DECL_OFFSET2(MonoDelegateTrampInfo,invoke_sig,24)
DECL_OFFSET2(MonoDelegateTrampInfo,sig,32)
DECL_OFFSET2(MonoDelegateTrampInfo,method_ptr,40)
DECL_OFFSET2(MonoDelegateTrampInfo,invoke_impl,48)
DECL_OFFSET2(MonoDelegateTrampInfo,impl_this,56)
DECL_OFFSET2(MonoDelegateTrampInfo,impl_nothis,64)
DECL_OFFSET2(MonoDelegateTrampInfo,need_rgctx_tramp,72)
DECL_OFFSET2(MonoDelegateTrampInfo,is_virtual,76)
DECL_SIZE2(GSharedVtCallInfo,40)
DECL_OFFSET2(GSharedVtCallInfo,addr,0)
DECL_OFFSET2(GSharedVtCallInfo,ret_marshal,8)
DECL_OFFSET2(GSharedVtCallInfo,vret_arg_reg,12)
DECL_OFFSET2(GSharedVtCallInfo,vret_slot,16)
DECL_OFFSET2(GSharedVtCallInfo,stack_usage,20)
DECL_OFFSET2(GSharedVtCallInfo,map_count,24)
DECL_OFFSET2(GSharedVtCallInfo,vcall_offset,28)
DECL_OFFSET2(GSharedVtCallInfo,calli,32)
DECL_OFFSET2(GSharedVtCallInfo,gsharedvt_in,36)
DECL_OFFSET2(GSharedVtCallInfo,map,40)
DECL_SIZE2(SeqPointInfo,8)
DECL_OFFSET2(SeqPointInfo,ss_tramp_addr,0)
DECL_OFFSET2(SeqPointInfo,bp_addrs,8)
DECL_SIZE2(DynCallArgs,144)
DECL_OFFSET2(DynCallArgs,res,0)
DECL_OFFSET2(DynCallArgs,ret,8)
DECL_OFFSET2(DynCallArgs,fregs,16)
DECL_OFFSET2(DynCallArgs,has_fp,80)
DECL_OFFSET2(DynCallArgs,nstack_args,88)
DECL_OFFSET2(DynCallArgs,regs,96)
DECL_SIZE2(MonoLMFTramp,40)
DECL_OFFSET2(MonoLMFTramp,lmf,0)
DECL_OFFSET2(MonoLMFTramp,ctx,24)
DECL_OFFSET2(MonoLMFTramp,lmf_addr,32)
DECL_SIZE2(CallContext,280)
DECL_OFFSET2(CallContext,gregs,0)
DECL_OFFSET2(CallContext,fregs,136)
DECL_OFFSET2(CallContext,stack_size,264)
DECL_OFFSET2(CallContext,stack,272)
DECL_SIZE2(MonoFtnDesc,32)
DECL_OFFSET2(MonoFtnDesc,addr,0)
DECL_OFFSET2(MonoFtnDesc,arg,8)
DECL_OFFSET2(MonoFtnDesc,method,16)
DECL_OFFSET2(MonoFtnDesc,interp_method,24)
#endif //disable jit check
#endif //cross compiler checks
#endif //gc check
#endif //TARGET_AMD64
#endif //TARGET_MACCAT
#endif //USED_CROSS_COMPILER_OFFSETS check
