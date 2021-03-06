

    def __enter__(self):
        return self

    
    def __exit__(self, type, value, traceback):
        self.Compile()

        
    def Compile(self, *args):
        if not args:
            with nogil:
                self.inst.get().Compile()
            return

        cdef void* callback = <void*> args[0]
        with nogil:
            self.inst.get().Compile(callback_wrapper, callback)


    def SetManifest(self, manifest):
        import json
        self.inst.get().SetManifestFromString(json.dumps(manifest))
