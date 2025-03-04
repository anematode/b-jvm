#include <natives-dsl.h>

DECLARE_NATIVE("java/awt", Toolkit, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/awt", Component, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/awt", Container, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/awt", Window, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/awt", Frame, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/awt", Cursor, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/awt", Font, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("sun/awt", X11GraphicsEnvironment, initDisplay, "(Z)V") { return value_null(); }

DECLARE_NATIVE("sun/awt", X11GraphicsEnvironment, initXRender, "(ZZ)Z") { return (stack_value) { .i = 1 }; }

DECLARE_NATIVE("sun/java2d", SurfaceData, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("sun/java2d/pipe", SpanClipRenderer, initIDs, "(Ljava/lang/Class;Ljava/lang/Class;)V") {
  return value_null();
}

DECLARE_NATIVE("sun/java2d/xr", XRSurfaceData, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("sun/java2d/loops", GraphicsPrimitiveMgr, initIDs, "(Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;Ljava/lang/Class;)V") {
  return value_null();
}

DECLARE_NATIVE("sun/java2d/loops", GraphicsPrimitiveMgr, registerNativeLoops, "()V") {
  return value_null();
}