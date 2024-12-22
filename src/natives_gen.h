/** BEGIN CODEGEN SECTION (gen_natives.c) */
struct bjvm_native_String {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *value;  // [C
  int32_t hash;  // I
};
struct bjvm_native_StackTraceElement {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *declaringClass;  // Ljava/lang/String;
  bjvm_obj_header *methodName;  // Ljava/lang/String;
  bjvm_obj_header *fileName;  // Ljava/lang/String;
  int32_t lineNumber;  // I
};
struct bjvm_native_Throwable {
  bjvm_obj_header base;
  // my fields
  bjvm_obj_header *backtrace;  // Ljava/lang/Object;
  bjvm_obj_header *detailMessage;  // Ljava/lang/String;
  bjvm_obj_header *cause;  // Ljava/lang/Throwable;
  bjvm_obj_header *stackTrace;  // [Ljava/lang/StackTraceElement;
  bjvm_obj_header *suppressedExceptions;  // Ljava/util/List;
};
struct bjvm_native_LambdaForm {
  bjvm_obj_header base;
  // my fields
  int32_t arity;  // I
  int32_t result;  // I
  int32_t forceInline;  // Z
  bjvm_obj_header *customized;  // Ljava/lang/invoke/MethodHandle;
  bjvm_obj_header *names;  // [Ljava/lang/invoke/LambdaForm$Name;
  bjvm_obj_header *debugName;  // Ljava/lang/String;
  bjvm_obj_header *vmentry;  // Ljava/lang/invoke/MemberName;
  int32_t isCompiled;  // Z
  bjvm_obj_header *transformCache;  // Ljava/lang/Object;
  int32_t invocationCounter;  // I
};
struct bjvm_native_Class {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_classdesc *reflected_class;

  // my fields
  bjvm_obj_header *cachedConstructor;  // Ljava/lang/reflect/Constructor;
  bjvm_obj_header *newInstanceCallerCache;  // Ljava/lang/Class;
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *classLoader;  // Ljava/lang/ClassLoader;
  bjvm_obj_header *reflectionData;  // Ljava/lang/ref/SoftReference;
  int32_t classRedefinedCount;  // I
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/ClassRepository;
  bjvm_obj_header *enumConstants;  // [Ljava/lang/Object;
  bjvm_obj_header *enumConstantDirectory;  // Ljava/util/Map;
  bjvm_obj_header *annotationData;  // Ljava/lang/Class$AnnotationData;
  bjvm_obj_header *annotationType;  // Lsun/reflect/annotation/AnnotationType;
  bjvm_obj_header *classValueMap;  // Ljava/lang/ClassValue$ClassValueMap;
};
struct bjvm_native_Field {
  bjvm_obj_header base;
  // superclass fields
  int32_t override;  // Z
  bjvm_obj_header *securityCheckCache;  // Ljava/lang/Object;
  // implementation-dependent fields
  bjvm_cp_field *reflected_field;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  int32_t slot;  // I
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *type;  // Ljava/lang/Class;
  int32_t modifiers;  // I
  bjvm_obj_header *signature;  // Ljava/lang/String;
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/FieldRepository;
  bjvm_obj_header *annotations;  // [B
  bjvm_obj_header *fieldAccessor;  // Lsun/reflect/FieldAccessor;
  bjvm_obj_header *overrideFieldAccessor;  // Lsun/reflect/FieldAccessor;
  bjvm_obj_header *root;  // Ljava/lang/reflect/Field;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
};
struct bjvm_native_Method {
  bjvm_obj_header base;
  // superclass fields
  int32_t override;  // Z
  bjvm_obj_header *securityCheckCache;  // Ljava/lang/Object;
  int32_t hasRealParameterData;  // Z
  bjvm_obj_header *parameters;  // [Ljava/lang/reflect/Parameter;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
  // implementation-dependent fields
  bjvm_cp_method *reflected_method;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  int32_t slot;  // I
  bjvm_obj_header *name;  // Ljava/lang/String;
  bjvm_obj_header *returnType;  // Ljava/lang/Class;
  bjvm_obj_header *parameterTypes;  // [Ljava/lang/Class;
  bjvm_obj_header *exceptionTypes;  // [Ljava/lang/Class;
  int32_t modifiers;  // I
  bjvm_obj_header *signature;  // Ljava/lang/String;
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/MethodRepository;
  bjvm_obj_header *annotations;  // [B
  bjvm_obj_header *parameterAnnotations;  // [B
  bjvm_obj_header *annotationDefault;  // [B
  bjvm_obj_header *methodAccessor;  // Lsun/reflect/MethodAccessor;
  bjvm_obj_header *root;  // Ljava/lang/reflect/Method;
};
struct bjvm_native_Constructor {
  bjvm_obj_header base;
  // superclass fields
  int32_t override;  // Z
  bjvm_obj_header *securityCheckCache;  // Ljava/lang/Object;
  int32_t hasRealParameterData;  // Z
  bjvm_obj_header *parameters;  // [Ljava/lang/reflect/Parameter;
  bjvm_obj_header *declaredAnnotations;  // Ljava/util/Map;
  // implementation-dependent fields
  bjvm_cp_method *reflected_ctor;

  // my fields
  bjvm_obj_header *clazz;  // Ljava/lang/Class;
  int32_t slot;  // I
  bjvm_obj_header *parameterTypes;  // [Ljava/lang/Class;
  bjvm_obj_header *exceptionTypes;  // [Ljava/lang/Class;
  int32_t modifiers;  // I
  bjvm_obj_header *signature;  // Ljava/lang/String;
  bjvm_obj_header *genericInfo;  // Lsun/reflect/generics/repository/ConstructorRepository;
  bjvm_obj_header *annotations;  // [B
  bjvm_obj_header *parameterAnnotations;  // [B
  bjvm_obj_header *constructorAccessor;  // Lsun/reflect/ConstructorAccessor;
  bjvm_obj_header *root;  // Ljava/lang/reflect/Constructor;
};
struct bjvm_native_Thread {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_thread *vm_thread;

  // my fields
  bjvm_obj_header *name;  // Ljava/lang/String;
  int32_t priority;  // I
  bjvm_obj_header *threadQ;  // Ljava/lang/Thread;
  int64_t eetop;  // J
  int32_t single_step;  // Z
  int32_t daemon;  // Z
  int32_t stillborn;  // Z
  bjvm_obj_header *target;  // Ljava/lang/Runnable;
  bjvm_obj_header *group;  // Ljava/lang/ThreadGroup;
  bjvm_obj_header *contextClassLoader;  // Ljava/lang/ClassLoader;
  bjvm_obj_header *inheritedAccessControlContext;  // Ljava/security/AccessControlContext;
  bjvm_obj_header *threadLocals;  // Ljava/lang/ThreadLocal$ThreadLocalMap;
  bjvm_obj_header *inheritableThreadLocals;  // Ljava/lang/ThreadLocal$ThreadLocalMap;
  int64_t stackSize;  // J
  int64_t nativeParkEventPointer;  // J
  int64_t tid;  // J
  int32_t threadStatus;  // I
  bjvm_obj_header *parkBlocker;  // Ljava/lang/Object;
  bjvm_obj_header *blocker;  // Lsun/nio/ch/Interruptible;
  bjvm_obj_header *blockerLock;  // Ljava/lang/Object;
  bjvm_obj_header *uncaughtExceptionHandler;  // Ljava/lang/Thread$UncaughtExceptionHandler;
  int64_t threadLocalRandomSeed;  // J
  int32_t threadLocalRandomProbe;  // I
  int32_t threadLocalRandomSecondarySeed;  // I
};
struct bjvm_native_MethodHandle {
  bjvm_obj_header base;
  // implementation-dependent fields
  bjvm_cp_method_handle_info *reflected_mh;

  // my fields
  bjvm_obj_header *type;  // Ljava/lang/invoke/MethodType;
  bjvm_obj_header *form;  // Ljava/lang/invoke/LambdaForm;
  bjvm_obj_header *asTypeCache;  // Ljava/lang/invoke/MethodHandle;
  int32_t customizationCount;  // B
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
  int32_t flags;  // I
  bjvm_obj_header *resolution;  // Ljava/lang/Object;
};
static inline void bjvm_register_native_padding(bjvm_vm *vm) {
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