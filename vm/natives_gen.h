/** BEGIN CODEGEN SECTION (gen_natives.c) */
struct bjvm_native_String {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *value;  // [B
  s32 coder;  // B
  s32 hash;  // I
  s32 hashIsZero;  // Z
};
struct bjvm_native_StackTraceElement {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *declaringClassObject;  // Ljava/lang/Class;
  bjvm_obj_header *classLoaderName;  // Ljava/lang/String;
  bjvm_obj_header *moduleName;  // Ljava/lang/String;
  bjvm_obj_header *moduleVersion;  // Ljava/lang/String;
  bjvm_obj_header *declaringClass;  // Ljava/lang/String;
  bjvm_obj_header *methodName;  // Ljava/lang/String;
  bjvm_obj_header *fileName;  // Ljava/lang/String;
  s32 lineNumber;  // I
  s32 format;  // B
};
struct bjvm_native_Throwable {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_cp_method *method;

  int faulting_insn;

  // my fields
  bjvm_obj_header *backtrace;  // Ljava/lang/Object;
  bjvm_obj_header *detailMessage;  // Ljava/lang/String;
  bjvm_obj_header *cause;  // Ljava/lang/Throwable;
  bjvm_obj_header *stackTrace;  // [Ljava/lang/StackTraceElement;
  s32 depth;  // I
  bjvm_obj_header *suppressedExceptions;  // Ljava/util/List;
};
struct bjvm_native_LambdaForm {
  bjvm_obj_header base;
  // my fields
  s32 arity;  // I
  s32 result;  // I
  s32 forceInline;  // Z
  bjvm_obj_header *customized;  // Ljava/lang/invoke/MethodHandle;
  bjvm_obj_header *names;  // [Ljava/lang/invoke/LambdaForm$Name;
  bjvm_obj_header *kind;  // Ljava/lang/invoke/LambdaForm$Kind;
  bjvm_obj_header *vmentry;  // Ljava/lang/invoke/MemberName;
  s32 isCompiled;  // Z
  bjvm_obj_header *transformCache;  // Ljava/lang/Object;
  s32 invocationCounter;  // I
};
struct bjvm_native_CallSite {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *target;  // Ljava/lang/invoke/MethodHandle;
  bjvm_obj_header *context;  // Ljava/lang/invoke/MethodHandleNatives$CallSiteContext;
};
struct bjvm_native_ConstantPool {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_classdesc *reflected_class;

  // my fields
  bjvm_obj_header *constantPoolOop;  // Ljava/lang/Object;
};
struct bjvm_native_Class {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_classdesc *reflected_class;

  // my fields
  bjvm_obj_header *cachedConstructor;  // Ljava/lang/reflect/Constructor;
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *module;  // Ljava/lang/Module;
  bjvm_obj_header *classLoader;  // Ljava/lang/ClassLoader;
  bjvm_obj_header *classData;  // Ljava/lang/Object;
  bjvm_obj_header *packageName;  // Ljava/lang/String;
  bjvm_obj_header *componentType;  // Ljava/lang/Class;
  bjvm_obj_header *reflectionData;  // Ljava/lang/ref/SoftReference;
  s32 classRedefinedCount;  // I
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/ClassRepository;
  bjvm_obj_header *enumConstants;  // [Ljava/lang/Object;
  bjvm_obj_header *enumConstantDirectory;  // Ljava/util/Map;
  bjvm_obj_header *annotationData;  // Ljava/lang/Class$AnnotationData;
  bjvm_obj_header *annotationType;  // Lsun/reflect/annotation/AnnotationType;
  bjvm_obj_header *classValueMap;  // Ljava/lang/ClassValue$ClassValueMap;
};
struct bjvm_native_Parameter {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *name;  // Ljava/lang/String;
  s32 modifiers;  // I
  bjvm_obj_header *executable;  // Ljava/lang/reflect/Executable;
  s32 index;  // I
  bjvm_obj_header *parameterTypeCache;  // Ljava/lang/reflect/Type;
  bjvm_obj_header *parameterClassCache;  // Ljava/lang/Class;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
};
struct bjvm_native_Field {
  bjvm_obj_header base;
  // superclass fields
  s32 override;  // Z
  bjvm_obj_header *accessCheckCache;  // Ljava/lang/Object;
  // implementation-dependent fields
  bjvm_cp_field *reflected_field;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  s32 slot;  // I
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *type;  // Ljava/lang/Class;
  s32 modifiers;  // I
  s32 trustedFinal;  // Z
  bjvm_obj_header *signature;  // Ljava/lang/String;
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/FieldRepository;
  bjvm_obj_header *annotations;  // [B
  bjvm_obj_header *fieldAccessor;  // Ljdk/internal/reflect/FieldAccessor;
  bjvm_obj_header *overrideFieldAccessor;  // Ljdk/internal/reflect/FieldAccessor;
  bjvm_obj_header *root;  // Ljava/lang/reflect/Field;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
};
struct bjvm_native_Method {
  bjvm_obj_header base;
  // superclass fields
  s32 override;  // Z
  bjvm_obj_header *accessCheckCache;  // Ljava/lang/Object;
  bjvm_obj_header *parameterData;  // Ljava/lang/reflect/Executable$ParameterData;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
  // implementation-dependent fields
  bjvm_cp_method *reflected_method;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  s32 slot;  // I
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *returnType;  // Ljava/lang/Class;
  bjvm_obj_header *parameterTypes;  // [Ljava/lang/Class;
  bjvm_obj_header *exceptionTypes;  // [Ljava/lang/Class;
  s32 modifiers;  // I
  bjvm_obj_header *signature;  // Ljava/lang/String;
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/MethodRepository;
  bjvm_obj_header *annotations;  // [B
  bjvm_obj_header *parameterAnnotations;  // [B
  bjvm_obj_header *annotationDefault;  // [B
  bjvm_obj_header *methodAccessor;  // Ljdk/internal/reflect/MethodAccessor;
  bjvm_obj_header *root;  // Ljava/lang/reflect/Method;
  s32 callerSensitive;  // B
};
struct bjvm_native_Constructor {
  bjvm_obj_header base;
  // superclass fields
  s32 override;  // Z
  bjvm_obj_header *accessCheckCache;  // Ljava/lang/Object;
  bjvm_obj_header *parameterData;  // Ljava/lang/reflect/Executable$ParameterData;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
  // implementation-dependent fields
  bjvm_cp_method *reflected_ctor;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  s32 slot;  // I
  bjvm_obj_header *parameterTypes;  // [Ljava/lang/Class;
  bjvm_obj_header *exceptionTypes;  // [Ljava/lang/Class;
  s32 modifiers;  // I
  bjvm_obj_header *signature;  // Ljava/lang/String;
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/ConstructorRepository;
  bjvm_obj_header *annotations;  // [B
  bjvm_obj_header *parameterAnnotations;  // [B
  bjvm_obj_header *constructorAccessor;  // Ljdk/internal/reflect/ConstructorAccessor;
  bjvm_obj_header *root;  // Ljava/lang/reflect/Constructor;
};
struct bjvm_native_Thread {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_thread *vm_thread;

  // my fields
  s64 eetop;  // J
  s64 tid;  // J
  bjvm_obj_header *name;  // Ljava/lang/String;
  s32 interrupted;  // Z
  bjvm_obj_header *contextClassLoader;  // Ljava/lang/ClassLoader;
  bjvm_obj_header *inheritedAccessControlContext;  // Ljava/security/AccessControlContext;
  bjvm_obj_header *holder;  // Ljava/lang/Thread$FieldHolder;
  bjvm_obj_header *threadLocals;  // Ljava/lang/ThreadLocal$ThreadLocalMap;
  bjvm_obj_header *inheritableThreadLocals;  // Ljava/lang/ThreadLocal$ThreadLocalMap;
  bjvm_obj_header *scopedValueBindings;  // Ljava/lang/Object;
  bjvm_obj_header *interruptLock;  // Ljava/lang/Object;
  bjvm_obj_header *parkBlocker;  // Ljava/lang/Object;
  bjvm_obj_header *nioBlocker;  // Lsun/nio/ch/Interruptible;
  bjvm_obj_header *cont;  // Ljdk/internal/vm/Continuation;
  bjvm_obj_header *uncaughtExceptionHandler;  // Ljava/lang/Thread$UncaughtExceptionHandler;
  s64 threadLocalRandomSeed;  // J
  s32 threadLocalRandomProbe;  // I
  s32 threadLocalRandomSecondarySeed;  // I
  bjvm_obj_header *container;  // Ljdk/internal/vm/ThreadContainer;
  bjvm_obj_header *headStackableScopes;  // Ljdk/internal/vm/StackableScope;
};
struct bjvm_native_MethodHandle {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_cp_method_handle_info *reflected_mh;

  // my fields
  bjvm_obj_header *type;  // Ljava/lang/invoke/MethodType;
  bjvm_obj_header *form;  // Ljava/lang/invoke/LambdaForm;
  bjvm_obj_header *asTypeCache;  // Ljava/lang/invoke/MethodHandle;
  bjvm_obj_header *asTypeSoftCache;  // Ljava/lang/ref/SoftReference;
  s32 customizationCount;  // B
  s32 updateInProgress;  // Z
};
struct bjvm_native_MethodType {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_cp_method_type_info *reflected_mt;

  // my fields
  bjvm_obj_header *rtype;  // Ljava/lang/Class;
  bjvm_obj_header *ptypes;  // [Ljava/lang/Class;
  bjvm_obj_header *form;  // Ljava/lang/invoke/MethodTypeForm;
  bjvm_obj_header *wrapAlt;  // Ljava/lang/Object;
  bjvm_obj_header *invokers;  // Ljava/lang/invoke/Invokers;
  bjvm_obj_header *methodDescriptor;  // Ljava/lang/String;
};
struct bjvm_native_MemberName {
  bjvm_obj_header base;
  // implementation-dependent fields
  void *vmtarget;

  intptr_t vmindex;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *type;  // Ljava/lang/Object;
  s32 flags;  // I
  bjvm_obj_header *method;  // Ljava/lang/invoke/ResolvedMethodName;
  bjvm_obj_header *resolution;  // Ljava/lang/Object;
};
struct bjvm_native_Reference {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *referent;  // Ljava/lang/Object;
  bjvm_obj_header *queue;  // Ljava/lang/ref/ReferenceQueue;
  bjvm_obj_header *next;  // Ljava/lang/ref/Reference;
  bjvm_obj_header *discovered;  // Ljava/lang/ref/Reference;
};
static inline void bjvm_register_native_padding(bjvm_vm *vm) {
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/Throwable", -1, (void*) (2 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "jdk/internal/reflect/ConstantPool", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/Class", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/reflect/Field", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/reflect/Method", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/reflect/Constructor", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/Thread", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/invoke/MethodHandle", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/invoke/MethodType", -1, (void*) (1 * sizeof(void*)));
  (void)bjvm_hash_table_insert(&vm->class_padding, "java/lang/invoke/MemberName", -1, (void*) (2 * sizeof(void*)));
}
/** END CODEGEN SECTION (gen_natives.c) */
