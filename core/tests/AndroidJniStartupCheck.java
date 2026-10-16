import android.os.Looper;
import dalvik.system.PathClassLoader;
import java.lang.reflect.Method;

public final class AndroidJniStartupCheck {
    public static void main(String[] arguments) throws Exception {
        if (arguments.length != 2) {
            throw new IllegalArgumentException("Usage: AndroidJniStartupCheck <apk> <native-library-directory>");
        }
        Looper.prepare();
        PathClassLoader loader = new PathClassLoader(arguments[0], arguments[1], AndroidJniStartupCheck.class.getClassLoader());
        Class<?> activityClass = Class.forName("org.sdrpp.sdrpp.MainActivity", true, loader);
        Object activity = activityClass.getDeclaredConstructor().newInstance();
        Method callback = activityClass.getDeclaredMethod("setNativeWindowInsets",
            int.class, int.class, int.class, int.class, int.class, int.class);
        callback.setAccessible(true);
        callback.invoke(activity, 0, 96, 0, 0, 1080, 2400);
        callback.invoke(activity, 96, 0, 0, 0, 2400, 1080);
        callback.invoke(activity, 0, 0, 96, 0, 2400, 1080);
        System.out.println("PASS: APK activity loads the native library and resolves window inset JNI callbacks");
    }
}
