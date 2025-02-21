

interface VMOptions {
    // Path to java home (if not set, defaults to cwd + /jdk23)
    javaHome: string;
    // Colon-separated list of paths or JAR files where classes may be found
    classpath: string;
    // Valid range: 1 << 14 to 1 << 30
    heapSize: number;
}

declare namespace java.lang {

}

type ValidClassNames<Classes> = any;
type LoadClass<Classes, T> = any;

declare class ObjectHandle {
    drop(): void;  // Explicitly drop the reference to this object
}

declare class VMThread<Classes> {
    // Get the VM associated with this thread.
    getVM(): VM<Classes>;

    // Load the class with the given fully qualified binary name. The class initializer, if any, is run on this thread.
    // Example usage:
    //   const ArrayList = thread.loadClass("java.lang.ArrayList");
    //   const list = new ArrayList<java_lang_String>();
    loadClass<T extends ValidClassNames<Classes>>(className: T): LoadClass<Classes, T>;

    // Explicitly drop the handle to the thread. This allows the thread to be GCed outside the normal.
    drop(): void;

    // Get the actual Java "java.lang.Thread" object associated with this Thread.
    getJavaObject(): any;
}

declare class VM<Classes> {
    // Creates a VM, and instantiates the root thread.
    // Exceptional behavior:
    //   - Throws a JS error on a failure to initialize the VM (e.g. due to resource exhaustion).
    static create<Classes>(options: VMOptions): VM<Classes>;

    // Make the thread the active thread, i.e., function calls on objects will be scheduled to this thread. If null,
    // the root thread is made the active thread.
    // Exceptional behavior:
    //   - Throws a JS error if the thread does not belong to this VM.
    makeActiveThread(thread: VMThread<Classes> | null)

    // Get the root thread.
    // Exceptional behavior: none.
    rootThread(): VMThread<Classes>;

    // Create a new thread.
    // Exceptional behavior:
    //   - Throws a JS error on resource exhaustion.
    createThread(): VMThread<Classes>;

    // Irreversibly destroy the VM. All active threads and handles are invalidated.
    // Exceptional behavior: none.
    dispose(): void;

    // Whether the VM is destroyed.
    isDestroyed(): boolean;
}