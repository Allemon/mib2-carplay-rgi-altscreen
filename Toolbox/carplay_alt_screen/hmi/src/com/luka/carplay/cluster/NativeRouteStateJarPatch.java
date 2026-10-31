package com.luka.carplay.cluster;

import java.nio.file.Files;
import java.nio.file.Paths;
import jdk.internal.org.objectweb.asm.*;

/** JDK8 build tool only. Preserve the OEM class and alter only setRgActive(Z)V.
 * Compile with -XDignore.symbol.file; never ship this tool on the vehicle. */
public final class NativeRouteStateJarPatch {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("expected OEM class input and patched class output");
        ClassReader reader = new ClassReader(Files.readAllBytes(Paths.get(args[0])));
        if (!reader.getClassName().equals("de/audi/tghu/navi/app/command/DSIResponseContainer"))
            throw new IllegalStateException("unexpected OEM class");
        final String overlay = "com/luka/carplay/routeguidance/NativeRouteStateOverlay";
        final int[] existing = {0}, changed = {0};
        reader.accept(new ClassVisitor(Opcodes.ASM5) {
            public MethodVisitor visitMethod(int a, String n, String d, String s, String[] e) {
                return new MethodVisitor(Opcodes.ASM5) {
                    public void visitMethodInsn(int o, String owner, String name, String desc, boolean itf) {
                        if (owner.equals(overlay)) existing[0]++;
                    }
                };
            }
        }, 0);
        if (existing[0] != 0) throw new IllegalStateException("native state patch already present");
        ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);
        reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
            public MethodVisitor visitMethod(int a, String n, String d, String s, String[] e) {
                MethodVisitor mv = super.visitMethod(a, n, d, s, e);
                if (!n.equals("setRgActive") || !d.equals("(Z)V")) return mv;
                if ((a & Opcodes.ACC_SYNCHRONIZED) == 0 || (a & Opcodes.ACC_STATIC) != 0)
                    throw new IllegalStateException("OEM setter must hold the container monitor");
                changed[0]++;
                return new MethodVisitor(Opcodes.ASM5, mv) {
                    public void visitCode() {
                        super.visitCode();
                        super.visitVarInsn(Opcodes.ALOAD, 0);
                        super.visitVarInsn(Opcodes.ILOAD, 1);
                        super.visitMethodInsn(Opcodes.INVOKESTATIC, overlay,
                            "nativeWrite", "(Ljava/lang/Object;Z)Z", false);
                        super.visitVarInsn(Opcodes.ISTORE, 1);
                    }
                };
            }
        }, 0);
        if (changed[0] != 1) throw new IllegalStateException("unexpected OEM setter shape");
        Files.write(Paths.get(args[1]), writer.toByteArray());
        System.out.println("NATIVE_ROUTE_STATE_PATCH=PASS");
    }
}
