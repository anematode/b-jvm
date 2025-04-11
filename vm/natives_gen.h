/** BEGIN CODEGEN SECTION (gen_natives.c) */
struct native_String {
  obj_header base;
  // my fields
  obj_header *value; // [B
  s32 coder;         // B
  s32 hash;          // I
  s32 hashIsZero;    // Z
};
struct native_StackTraceElement {
  obj_header base;
  // my fields
  obj_header *declaringClassObject; // Ljava/lang/Class;
  obj_header *classLoaderName;      // Ljava/lang/String;
  obj_header *moduleName;           // Ljava/lang/String;
  obj_header *moduleVersion;        // Ljava/lang/String;
  obj_header *declaringClass;       // Ljava/lang/String;
  obj_header *methodName;           // Ljava/lang/String;
  obj_header *fileName;             // Ljava/lang/String;
  s32 lineNumber;                   // I
  s32 format;                       // B
};
struct native_Throwable {
  obj_header base;
  // implementation-dependent fields
  cp_method *method;

  int faulting_insn;

  // my fields
  obj_header *backtrace;            // Ljava/lang/Object;
  obj_header *detailMessage;        // Ljava/lang/String;
  obj_header *cause;                // Ljava/lang/Throwable;
  obj_header *stackTrace;           // [Ljava/lang/StackTraceElement;
  s32 depth;                        // I
  obj_header *suppressedExceptions; // Ljava/util/List;
};
struct native_LambdaForm {
  obj_header base;
  // my fields
  s32 arity;                  // I
  s32 result;                 // I
  s32 forceInline;            // Z
  obj_header *customized;     // Ljava/lang/invoke/MethodHandle;
  obj_header *names;          // [Ljava/lang/invoke/LambdaForm$Name;
  obj_header *kind;           // Ljava/lang/invoke/LambdaForm$Kind;
  obj_header *vmentry;        // Ljava/lang/invoke/MemberName;
  s32 isCompiled;             // Z
  obj_header *transformCache; // Ljava/lang/Object;
  s32 invocationCounter;      // I
};
struct native_CallSite {
  obj_header base;
  // my fields
  obj_header *target;  // Ljava/lang/invoke/MethodHandle;
  obj_header *context; // Ljava/lang/invoke/MethodHandleNatives$CallSiteContext;
};
struct native_ConstantPool {
  obj_header base;
  // implementation-dependent fields
  classdesc *reflected_class;

  // my fields
  obj_header *constantPoolOop; // Ljava/lang/Object;
};
struct native_Class {
  obj_header base;
  // implementation-dependent fields
  classdesc *reflected_class;

  // my fields
  obj_header *cachedConstructor;     // Ljava/lang/reflect/Constructor;
  obj_header *name;                  // Ljava/lang/String;
  obj_header *module;                // Ljava/lang/Module;
  obj_header *classLoader;           // Ljava/lang/ClassLoader;
  obj_header *classData;             // Ljava/lang/Object;
  obj_header *packageName;           // Ljava/lang/String;
  obj_header *componentType;         // Ljava/lang/Class;
  obj_header *reflectionData;        // Ljava/lang/ref/SoftReference;
  s32 classRedefinedCount;           // I
  obj_header *genericInfo;           // Lsun/reflect/generics/repository/ClassRepository;
  obj_header *enumConstants;         // [Ljava/lang/Object;
  obj_header *enumConstantDirectory; // Ljava/util/Map;
  obj_header *annotationData;        // Ljava/lang/Class$AnnotationData;
  obj_header *annotationType;        // Lsun/reflect/annotation/AnnotationType;
  obj_header *classValueMap;         // Ljava/lang/ClassValue$ClassValueMap;
};
struct native_ClassLoader {
  obj_header base;
  // implementation-dependent fields
  classloader *reflected_loader;

  // my fields
  obj_header *parent;                 // Ljava/lang/ClassLoader;
  obj_header *name;                   // Ljava/lang/String;
  obj_header *unnamedModule;          // Ljava/lang/Module;
  obj_header *nameAndId;              // Ljava/lang/String;
  obj_header *parallelLockMap;        // Ljava/util/concurrent/ConcurrentHashMap;
  obj_header *package2certs;          // Ljava/util/concurrent/ConcurrentHashMap;
  obj_header *classes;                // Ljava/util/ArrayList;
  obj_header *defaultDomain;          // Ljava/security/ProtectionDomain;
  obj_header *packages;               // Ljava/util/concurrent/ConcurrentHashMap;
  obj_header *libraries;              // Ljdk/internal/loader/NativeLibraries;
  obj_header *assertionLock;          // Ljava/lang/Object;
  s32 defaultAssertionStatus;         // Z
  obj_header *packageAssertionStatus; // Ljava/util/Map;
  obj_header *classAssertionStatus;   // Ljava/util/Map;
  obj_header *classLoaderValueMap;    // Ljava/util/concurrent/ConcurrentHashMap;
};
struct native_Parameter {
  obj_header base;
  // my fields
  obj_header *name;                // Ljava/lang/String;
  s32 modifiers;                   // I
  obj_header *executable;          // Ljava/lang/reflect/Executable;
  s32 index;                       // I
  obj_header *parameterTypeCache;  // Ljava/lang/reflect/Type;
  obj_header *parameterClassCache; // Ljava/lang/Class;
  obj_header *declaredAnnotations; // Ljava/util/Map;
};
struct native_Field {
  obj_header base;
  // superclass fields
  s32 override;                 // Z
  obj_header *accessCheckCache; // Ljava/lang/Object;
  // implementation-dependent fields
  cp_field *reflected_field;

  // my fields
  obj_header *clazz;                 // Ljava/lang/Class;
  s32 slot;                          // I
  obj_header *name;                  // Ljava/lang/String;
  obj_header *type;                  // Ljava/lang/Class;
  s32 modifiers;                     // I
  s32 trustedFinal;                  // Z
  obj_header *signature;             // Ljava/lang/String;
  obj_header *genericInfo;           // Lsun/reflect/generics/repository/FieldRepository;
  obj_header *annotations;           // [B
  obj_header *fieldAccessor;         // Ljdk/internal/reflect/FieldAccessor;
  obj_header *overrideFieldAccessor; // Ljdk/internal/reflect/FieldAccessor;
  obj_header *root;                  // Ljava/lang/reflect/Field;
  obj_header *declaredAnnotations;   // Ljava/util/Map;
};
struct native_Method {
  obj_header base;
  // superclass fields
  s32 override;                    // Z
  obj_header *accessCheckCache;    // Ljava/lang/Object;
  obj_header *parameterData;       // Ljava/lang/reflect/Executable$ParameterData;
  obj_header *declaredAnnotations; // Ljava/util/Map;
  // implementation-dependent fields
  cp_method *reflected_method;

  // my fields
  obj_header *clazz;                // Ljava/lang/Class;
  s32 slot;                         // I
  obj_header *name;                 // Ljava/lang/String;
  obj_header *returnType;           // Ljava/lang/Class;
  obj_header *parameterTypes;       // [Ljava/lang/Class;
  obj_header *exceptionTypes;       // [Ljava/lang/Class;
  s32 modifiers;                    // I
  obj_header *signature;            // Ljava/lang/String;
  obj_header *genericInfo;          // Lsun/reflect/generics/repository/MethodRepository;
  obj_header *annotations;          // [B
  obj_header *parameterAnnotations; // [B
  obj_header *annotationDefault;    // [B
  obj_header *methodAccessor;       // Ljdk/internal/reflect/MethodAccessor;
  obj_header *root;                 // Ljava/lang/reflect/Method;
  s32 callerSensitive;              // B
};
struct native_Constructor {
  obj_header base;
  // superclass fields
  s32 override;                    // Z
  obj_header *accessCheckCache;    // Ljava/lang/Object;
  obj_header *parameterData;       // Ljava/lang/reflect/Executable$ParameterData;
  obj_header *declaredAnnotations; // Ljava/util/Map;
  // implementation-dependent fields
  cp_method *reflected_ctor;

  // my fields
  obj_header *clazz;                // Ljava/lang/Class;
  s32 slot;                         // I
  obj_header *parameterTypes;       // [Ljava/lang/Class;
  obj_header *exceptionTypes;       // [Ljava/lang/Class;
  s32 modifiers;                    // I
  obj_header *signature;            // Ljava/lang/String;
  obj_header *genericInfo;          // Lsun/reflect/generics/repository/ConstructorRepository;
  obj_header *annotations;          // [B
  obj_header *parameterAnnotations; // [B
  obj_header *constructorAccessor;  // Ljdk/internal/reflect/ConstructorAccessor;
  obj_header *root;                 // Ljava/lang/reflect/Constructor;
};
struct native_Thread {
  obj_header base;
  // implementation-dependent fields
  vm_thread *vm_thread;

  // my fields
  volatile s64 eetop;                        // J
  s64 tid;                                   // J
  obj_header *name;                          // Ljava/lang/String;
  volatile s32 interrupted;                  // Z
  obj_header *contextClassLoader;            // Ljava/lang/ClassLoader;
  obj_header *inheritedAccessControlContext; // Ljava/security/AccessControlContext;
  obj_header *holder;                        // Ljava/lang/Thread$FieldHolder;
  obj_header *threadLocals;                  // Ljava/lang/ThreadLocal$ThreadLocalMap;
  obj_header *inheritableThreadLocals;       // Ljava/lang/ThreadLocal$ThreadLocalMap;
  obj_header *scopedValueBindings;           // Ljava/lang/Object;
  obj_header *interruptLock;                 // Ljava/lang/Object;
  obj_header *parkBlocker;                   // Ljava/lang/Object;
  obj_header *nioBlocker;                    // Lsun/nio/ch/Interruptible;
  obj_header *cont;                          // Ljdk/internal/vm/Continuation;
  obj_header *uncaughtExceptionHandler;      // Ljava/lang/Thread$UncaughtExceptionHandler;
  s64 threadLocalRandomSeed;                 // J
  s32 threadLocalRandomProbe;                // I
  s32 threadLocalRandomSecondarySeed;        // I
  obj_header *container;                     // Ljdk/internal/vm/ThreadContainer;
  obj_header *headStackableScopes;           // Ljdk/internal/vm/StackableScope;
};
struct native_MethodHandle {
  obj_header base;
  // implementation-dependent fields
  cp_method_handle_info *reflected_mh;

  // my fields
  obj_header *type;            // Ljava/lang/invoke/MethodType;
  obj_header *form;            // Ljava/lang/invoke/LambdaForm;
  obj_header *asTypeCache;     // Ljava/lang/invoke/MethodHandle;
  obj_header *asTypeSoftCache; // Ljava/lang/ref/SoftReference;
  s32 customizationCount;      // B
  s32 updateInProgress;        // Z
};
struct native_VarHandle {
  obj_header base;
  // my fields
  obj_header *vform;             // Ljava/lang/invoke/VarForm;
  s32 exact;                     // Z
  obj_header *methodTypeTable;   // [Ljava/lang/invoke/MethodType;
  obj_header *methodHandleTable; // [Ljava/lang/invoke/MethodHandle;
};
struct native_MethodType {
  obj_header base;
  // implementation-dependent fields
  cp_method_type_info *reflected_mt;

  // my fields
  obj_header *rtype;            // Ljava/lang/Class;
  obj_header *ptypes;           // [Ljava/lang/Class;
  obj_header *form;             // Ljava/lang/invoke/MethodTypeForm;
  obj_header *wrapAlt;          // Ljava/lang/Object;
  obj_header *invokers;         // Ljava/lang/invoke/Invokers;
  obj_header *methodDescriptor; // Ljava/lang/String;
};
struct native_VarForm {
  obj_header base;
  // my fields
  obj_header *implClass;          // Ljava/lang/Class;
  obj_header *methodType_table;   // [Ljava/lang/invoke/MethodType;
  obj_header *memberName_table;   // [Ljava/lang/invoke/MemberName;
  obj_header *methodType_V_table; // [Ljava/lang/invoke/MethodType;
};
struct native_MemberName {
  obj_header base;
  // implementation-dependent fields
  void *vmtarget;

  intptr_t vmindex;

  // my fields
  obj_header *clazz;      // Ljava/lang/Class;
  obj_header *name;       // Ljava/lang/String;
  obj_header *type;       // Ljava/lang/Object;
  s32 flags;              // I
  obj_header *method;     // Ljava/lang/invoke/ResolvedMethodName;
  obj_header *resolution; // Ljava/lang/Object;
};
struct native_Reference {
  obj_header base;
  // my fields
  obj_header *referent;   // Ljava/lang/Object;
  obj_header *queue;      // Ljava/lang/ref/ReferenceQueue;
  obj_header *next;       // Ljava/lang/ref/Reference;
  obj_header *discovered; // Ljava/lang/ref/Reference;
};
static inline void register_native_padding(vm *vm) {
  (void)hash_table_insert(&vm->class_padding, "java/lang/String", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/StackTraceElement", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/Throwable", -1, (void *)(2 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/LambdaForm", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/CallSite", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "jdk/internal/reflect/ConstantPool", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/Class", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/ClassLoader", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/reflect/Parameter", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/reflect/Field", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/reflect/Method", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/reflect/Constructor", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/Thread", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/MethodHandle", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/VarHandle", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/MethodType", -1, (void *)(1 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/VarForm", -1, (void *)(0 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/invoke/MemberName", -1, (void *)(2 * sizeof(void *)));
  (void)hash_table_insert(&vm->class_padding, "java/lang/ref/Reference", -1, (void *)(0 * sizeof(void *)));
}
/** END CODEGEN SECTION (gen_natives.c) */