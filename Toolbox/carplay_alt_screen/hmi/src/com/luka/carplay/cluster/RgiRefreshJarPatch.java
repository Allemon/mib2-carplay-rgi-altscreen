package com.luka.carplay.cluster;

import java.nio.file.Files;
import java.nio.file.Paths;
import jdk.internal.org.objectweb.asm.*;

/** JDK8 build tool, never packaged on the vehicle. Patch the existing OEM
 * adapter without replacing its other methods or proprietary dependencies.
 * Compile with javac -XDignore.symbol.file, then pass input/output class paths.
 */
public final class RgiRefreshJarPatch {
    public static void main(String[] args) throws Exception {
        final String controller = "com/luka/carplay/cluster/ClusterStateController";
        ClassReader reader = new ClassReader(Files.readAllBytes(Paths.get(args[0])));
        final int[] existing = {0}, inserted = {0}, methods = {0};
        reader.accept(new ClassVisitor(Opcodes.ASM5) {
            public MethodVisitor visitMethod(int a, String n, String d, String s, String[] e) {
                if (!n.equals("defineContexts") || !d.equals("()V")) return null;
                return new MethodVisitor(Opcodes.ASM5) {
                    public void visitMethodInsn(int o, String owner, String name, String desc, boolean itf) {
                        if (owner.equals(controller) && name.equals("configureRgiRefreshContext")) existing[0]++;
                    }
                };
            }
        }, 0);
        if (existing[0] != 0) throw new IllegalStateException("refresh patch already present");
        ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);
        reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
            public MethodVisitor visitMethod(int a, String n, String d, String s, String[] e) {
                MethodVisitor target = super.visitMethod(a, n, d, s, e);
                if (!n.equals("defineContexts") || !d.equals("()V")) return target;
                methods[0]++;
                return new MethodVisitor(Opcodes.ASM5, target) {
                    public void visitInsn(int opcode) {
                        if (opcode == Opcodes.RETURN) {
                            super.visitVarInsn(Opcodes.ALOAD, 0);
                            super.visitMethodInsn(Opcodes.INVOKESTATIC, controller,
                                "configureRgiRefreshContext", "(Ljava/lang/Object;)V", false);
                            inserted[0]++;
                        }
                        super.visitInsn(opcode);
                    }
                };
            }
        }, 0);
        if (methods[0] != 1 || inserted[0] != 1) throw new IllegalStateException("unexpected OEM method shape");
        Files.write(Paths.get(args[1]), writer.toByteArray());
        System.out.println("RGI_REFRESH_PATCH=PASS before_initial_declare=1 OEM_methods_preserved=1");
    }
}
