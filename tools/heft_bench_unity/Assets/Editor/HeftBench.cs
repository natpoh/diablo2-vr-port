// The heft bench: our weapon-weight model (cleanroom/heft, through heft_bench.dll)
// against a PhysX reference built the way HurricaneVR holds a weapon - none of
// Hurricane's code, its arrangement and its numbers:
//
//   a kinematic wrist rides the controller, teleported with it (Hurricane's "LeftOffset",
//   a child of the controller);
//   a 2 kg hand, no gravity, hangs on a ConfigurableJoint from it with a linear
//   and a slerp drive (HVR_DefaultHandStrength 3000/300/300, 500/50/75; on a large
//   two-handed weapon each hand 3000/300/300, 200/10/50), the drives' target
//   velocities set to the controller's every step (HVRJointHand.UpdateTargetVelocity);
//   the weapon's rigidbody joined to the hand linear-locked with a stiff slerp drive
//   (HVR_GrabbableSettings: 100000/1000/100000, projection 0.01);
//   physics at 90 Hz, solver 16/12 (the hand 10/10), weapons capped at 30 rad/s.
//
// Both are driven by the same controller paths (cleanroom/heft/bench.cpp) and
// written to <out>/<weapon>_<path>.csv; `heft_test compare <out>` reads them.
//
//   Unity -batchmode -nographics -projectPath tools/heft_bench_unity
//         -executeMethod HeftBench.RunAll -heftOut <dir> -quit

using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class HeftBench
{
    const string Dll = "heft_bench";
    const double Dt = 1.0 / 90.0;

    [DllImport(Dll)] static extern int heft_weapon_count();
    [DllImport(Dll)] static extern IntPtr heft_weapon_name(int w);
    [DllImport(Dll)] static extern int heft_weapon_two_handed(int w);
    [DllImport(Dll)] static extern void heft_weapon_steel(int w, out double mass, double[] com3, double[] moments3, double[] rot4);
    [DllImport(Dll)] static extern void heft_weapon_points(int w, double[] tip3, double[] second3);
    [DllImport(Dll)] static extern int heft_path_count();
    [DllImport(Dll)] static extern IntPtr heft_path_name(int p);
    [DllImport(Dll)] static extern double heft_path_duration(int p);
    [DllImport(Dll)] static extern int heft_path_two_hands(int p);
    [DllImport(Dll)] static extern void heft_controller(int p, int w, int hand, double t, double[] pos3, double[] rot4);
    [DllImport(Dll)] static extern void heft_muscle(int twoHands, double[] six);
    [DllImport(Dll)] static extern IntPtr heft_sim_new(int w, int twoHands, double substep);
    [DllImport(Dll)] static extern void heft_sim_step(IntPtr sim, double[] poses, double[] vels, double dt);
    [DllImport(Dll)] static extern void heft_sim_grip(IntPtr sim, double[] pos3, double[] rot4);
    [DllImport(Dll)] static extern void heft_sim_free(IntPtr sim);

    const float HandMass = 2f;
    static readonly Vector3 HandInertia = Vector3.one * (0.4f * 2f * 0.05f * 0.05f);   // as the model's 5 cm ball

    static Vector3 V(double[] a) => new Vector3((float)a[0], (float)a[1], (float)a[2]);
    static Quaternion Q(double[] a) => new Quaternion((float)a[0], (float)a[1], (float)a[2], (float)a[3]);

    public static void RunAll()
    {
        string outDir = Arg("-heftOut") ?? Path.Combine(Application.dataPath, "..", "results");
        Directory.CreateDirectory(outDir);
        Physics.simulationMode = SimulationMode.Script;
        Physics.gravity = new Vector3(0, -9.81f, 0);
        // -heftIterations n: the reference with another solver count, to see how far PhysX
        // moves from itself (the flailing runs are chaotic)
        int iters = int.TryParse(Arg("-heftIterations"), out var it) ? it : 16;
        Physics.defaultSolverIterations = iters;
        Physics.defaultSolverVelocityIterations = iters * 3 / 4;
        Physics.defaultMaxAngularSpeed = 30;
        int runs = 0;
        for (int w = 0; w < heft_weapon_count(); ++w)
            for (int p = 0; p < heft_path_count(); ++p)
            {
                bool two = heft_path_two_hands(p) != 0;
                if (two && heft_weapon_two_handed(w) == 0) continue;
                RunOne(w, p, outDir);
                ++runs;
            }
        Debug.Log($"heft bench: {runs} runs written to {Path.GetFullPath(outDir)}");
        if (Application.isBatchMode) EditorApplication.Exit(0);
    }

    // Why the reference turns softer than the drives say: a broadsword held still, its
    // sag in degrees, with the hand's inertia, the solver's iterations, or the hand
    // itself taken out of the chain (the weapon on the wrist's drive directly).
    public static void Diagnose()
    {
        Physics.simulationMode = SimulationMode.Script;
        Physics.gravity = new Vector3(0, -9.81f, 0);
        Physics.defaultMaxAngularSpeed = 30;
        foreach (var (label, inertiaScale, iters, direct) in new[] {
                     ("as the bench", 1f, 16, false), ("hand inertia x10", 10f, 16, false), ("hand inertia x100", 100f, 16, false),
                     ("solver 64/64", 1f, 64, false), ("no hand: weapon on the drive", 1f, 16, true) })
        {
            Physics.defaultSolverIterations = iters;
            Physics.defaultSolverVelocityIterations = iters;
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            int w = 1;   // broadsword
            Pose(0, w, 0, 0, out var cp, out var cr);
            var wrist = new GameObject("wrist").AddComponent<Rigidbody>();
            wrist.isKinematic = true;
            wrist.transform.SetPositionAndRotation(cp, cr);
            var steel = new GameObject("steel");
            steel.transform.SetPositionAndRotation(cp, cr);
            var wb = steel.AddComponent<Rigidbody>();
            var com = new double[3];
            var moments = new double[3];
            var axes = new double[4];
            heft_weapon_steel(w, out double mass, com, moments, axes);
            wb.mass = (float)mass + (direct ? HandMass : 0);
            wb.centerOfMass = V(com);
            wb.inertiaTensor = V(moments);
            wb.inertiaTensorRotation = Q(axes);
            wb.maxAngularVelocity = 30;
            Rigidbody driven = wb;
            if (!direct)
            {
                var hand = new GameObject("hand").AddComponent<Rigidbody>();
                hand.transform.SetPositionAndRotation(cp, cr);
                hand.mass = HandMass;
                hand.inertiaTensor = HandInertia * inertiaScale;
                hand.useGravity = false;
                hand.solverIterations = Math.Max(10, iters);
                hand.solverVelocityIterations = Math.Max(10, iters);
                var g = steel.AddComponent<ConfigurableJoint>();
                g.autoConfigureConnectedAnchor = false;
                g.connectedBody = hand;
                g.anchor = Vector3.zero;
                g.connectedAnchor = Vector3.zero;
                g.xMotion = g.yMotion = g.zMotion = ConfigurableJointMotion.Locked;
                g.rotationDriveMode = RotationDriveMode.Slerp;
                g.slerpDrive = Drive(100000, 1000, 100000);
                g.projectionMode = JointProjectionMode.PositionAndRotation;
                g.projectionDistance = 0.01f;
                g.projectionAngle = 0.01f;
                driven = hand;
            }
            var j = wrist.gameObject.AddComponent<ConfigurableJoint>();
            j.autoConfigureConnectedAnchor = false;
            j.connectedBody = driven;
            j.anchor = Vector3.zero;
            j.connectedAnchor = Vector3.zero;
            j.rotationDriveMode = RotationDriveMode.Slerp;
            j.xDrive = j.yDrive = j.zDrive = Drive(3000, 300, 300);
            j.slerpDrive = Drive(500, 50, 75);
            for (int k = 0; k < 270; ++k) Physics.Simulate((float)Dt);
            float sag = Vector3.Angle(wb.rotation * Vector3.up, cr * Vector3.up);
            Debug.Log($"heft diagnose: {label}: sag {sag:F2} deg (500 N m/rad alone would give {Mathf.Rad2Deg * 9.81f * (float)mass * V(com).y / 500f:F2})");
        }
        if (Application.isBatchMode) EditorApplication.Exit(0);
    }

    // What torque a slerp drive really gives in PhysX: a body with a known inertia,
    // no gravity, turned off its target by an angle and let go; the first step's
    // angular acceleration is the drive's torque at that angle. The same for the
    // linear drive at an offset.
    public static void Identify()
    {
        Physics.simulationMode = SimulationMode.Script;
        Physics.gravity = Vector3.zero;
        Physics.defaultSolverIterations = 16;
        Physics.defaultSolverVelocityIterations = 12;
        Physics.defaultMaxAngularSpeed = 1000;
        const float I = 0.25f, M = 2f;
        foreach (var (spring, damper, max) in new[] { (500f, 0f, 1e9f), (200f, 0f, 1e9f), (500f, 0f, 75f), (500f, 50f, 1e9f) })
            foreach (float deg in new[] { 2f, 10f, 30f, 60f, 90f, 150f })
            {
                EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
                var wrist = new GameObject("wrist").AddComponent<Rigidbody>();
                wrist.isKinematic = true;
                var body = new GameObject("body").AddComponent<Rigidbody>();
                body.mass = M;
                body.inertiaTensor = new Vector3(I, I, I);
                body.centerOfMass = Vector3.zero;
                body.maxAngularVelocity = 1000;
                var j = wrist.gameObject.AddComponent<ConfigurableJoint>();
                j.autoConfigureConnectedAnchor = false;
                j.connectedBody = body;
                j.anchor = j.connectedAnchor = Vector3.zero;
                j.rotationDriveMode = RotationDriveMode.Slerp;
                j.xDrive = j.yDrive = j.zDrive = Drive(3000, 0, 1e9f);
                j.slerpDrive = Drive(spring, damper, max);
                body.rotation = Quaternion.AngleAxis(deg, Vector3.right);   // off the target after the joint was made
                body.angularVelocity = Vector3.zero;
                Physics.Simulate((float)Dt);
                float w = body.angularVelocity.magnitude;
                float torque = I * w / (float)Dt;
                Debug.Log($"heft identify: slerp k {spring} c {damper} max {max}: at {deg} deg torque {torque:F1} N m, k*theta {spring * deg * Mathf.Deg2Rad:F1}, ratio {torque / (spring * deg * Mathf.Deg2Rad):F3}");
            }
        foreach (float off in new[] { 0.01f, 0.05f, 0.2f })
        {
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            var wrist = new GameObject("wrist").AddComponent<Rigidbody>();
            wrist.isKinematic = true;
            var body = new GameObject("body").AddComponent<Rigidbody>();
            body.mass = M;
            body.inertiaTensor = new Vector3(I, I, I);
            var j = wrist.gameObject.AddComponent<ConfigurableJoint>();
            j.autoConfigureConnectedAnchor = false;
            j.connectedBody = body;
            j.anchor = j.connectedAnchor = Vector3.zero;
            j.rotationDriveMode = RotationDriveMode.Slerp;
            j.xDrive = j.yDrive = j.zDrive = Drive(3000, 0, 1e9f);
            j.slerpDrive = Drive(500, 0, 1e9f);
            body.position = new Vector3(off, 0, 0);
            Physics.Simulate((float)Dt);
            float force = M * body.linearVelocity.magnitude / (float)Dt;
            Debug.Log($"heft identify: linear k 3000 at {off} m: force {force:F1} N, k*x {3000 * off:F1}, ratio {force / (3000 * off):F3}");
        }
        if (Application.isBatchMode) EditorApplication.Exit(0);
    }

    static string Arg(string name)
    {
        var a = Environment.GetCommandLineArgs();
        for (int i = 0; i + 1 < a.Length; ++i) if (a[i] == name) return a[i + 1];
        return null;
    }

    static void Pose(int p, int w, int hand, double t, out Vector3 pos, out Quaternion rot)
    {
        var a = new double[3];
        var b = new double[4];
        heft_controller(p, w, hand, t, a, b);
        pos = V(a);
        rot = Q(b);
    }

    static JointDrive Drive(double spring, double damper, double max) =>
        new JointDrive { positionSpring = (float)spring, positionDamper = (float)damper, maximumForce = (float)max };

    static void RunOne(int w, int p, string outDir)
    {
        EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        string wName = Marshal.PtrToStringAnsi(heft_weapon_name(w)), pName = Marshal.PtrToStringAnsi(heft_path_name(p));
        bool two = heft_path_two_hands(p) != 0;
        int n = two ? 2 : 1;
        var muscle = new double[6];
        heft_muscle(two ? 1 : 0, muscle);
        var tip = new double[3];
        var second = new double[3];
        heft_weapon_points(w, tip, second);
        Vector3[] gripAt = { Vector3.zero, V(second) };

        // the weapon: the steel alone, its frame the grip frame, placed in the main hand
        Pose(p, w, 0, 0, out var c0, out var r0);
        var steel = new GameObject(wName);
        steel.transform.SetPositionAndRotation(c0, r0);
        var wb = steel.AddComponent<Rigidbody>();
        var com = new double[3];
        var moments = new double[3];
        var axes = new double[4];
        heft_weapon_steel(w, out double mass, com, moments, axes);
        wb.mass = (float)mass;
        wb.centerOfMass = V(com);
        wb.inertiaTensor = V(moments);
        wb.inertiaTensorRotation = Q(axes);
        wb.useGravity = true;
        wb.linearDamping = 0;
        wb.angularDamping = 0;
        wb.maxAngularVelocity = 30;
        wb.interpolation = RigidbodyInterpolation.None;

        var wrists = new Rigidbody[n];
        var handJoints = new ConfigurableJoint[n];
        var lastPos = new Vector3[n];
        var lastRot = new Quaternion[n];
        for (int i = 0; i < n; ++i)
        {
            Pose(p, w, i, 0, out var cp, out var cr);
            var wrist = new GameObject("wrist " + i);
            wrist.transform.SetPositionAndRotation(cp, cr);
            wrists[i] = wrist.AddComponent<Rigidbody>();
            wrists[i].isKinematic = true;
            wrists[i].useGravity = false;

            var hand = new GameObject("hand " + i);
            hand.transform.SetPositionAndRotation(cp, cr);
            var hb = hand.AddComponent<Rigidbody>();
            hb.mass = HandMass;
            hb.inertiaTensor = HandInertia;
            hb.inertiaTensorRotation = Quaternion.identity;
            hb.centerOfMass = Vector3.zero;
            hb.useGravity = false;
            hb.linearDamping = 0;
            hb.angularDamping = 0;
            hb.maxAngularVelocity = 150;
            hb.solverIterations = 10;
            hb.solverVelocityIterations = 10;

            // the hand on its wrist (HVRJointHand.SetupJoint)
            var j = wrist.AddComponent<ConfigurableJoint>();
            j.autoConfigureConnectedAnchor = false;
            j.connectedBody = hb;
            j.connectedAnchor = Vector3.zero;
            j.anchor = Vector3.zero;
            j.enableCollision = false;
            j.enablePreprocessing = false;
            j.rotationDriveMode = RotationDriveMode.Slerp;
            j.xMotion = j.yMotion = j.zMotion = ConfigurableJointMotion.Free;
            j.angularXMotion = j.angularYMotion = j.angularZMotion = ConfigurableJointMotion.Free;
            j.xDrive = j.yDrive = j.zDrive = Drive(muscle[0], muscle[1], muscle[2]);
            j.slerpDrive = Drive(muscle[3], muscle[4], muscle[5]);
            j.targetRotation = Quaternion.identity;
            handJoints[i] = j;

            // the weapon on the hand (HVRHandGrabber.SetupConfigurableJoint, HVR_GrabbableSettings)
            var g = steel.AddComponent<ConfigurableJoint>();
            g.autoConfigureConnectedAnchor = false;
            g.connectedBody = hb;
            g.anchor = gripAt[i];
            g.connectedAnchor = Vector3.zero;
            g.xMotion = g.yMotion = g.zMotion = ConfigurableJointMotion.Locked;
            g.angularXMotion = g.angularYMotion = g.angularZMotion = ConfigurableJointMotion.Free;
            g.rotationDriveMode = RotationDriveMode.Slerp;
            g.slerpDrive = Drive(100000, 1000, 100000);
            g.targetRotation = Quaternion.identity;
            g.projectionMode = JointProjectionMode.PositionAndRotation;
            g.projectionDistance = 0.01f;
            g.projectionAngle = 0.01f;
            g.enablePreprocessing = true;

            lastPos[i] = cp;
            lastRot[i] = cr;
        }

        // the model at PhysX's own step by default (-heftSubstep 0.002777 for the mod's 1/360)
        double substep = double.TryParse(Arg("-heftSubstep"), NumberStyles.Float, CultureInfo.InvariantCulture, out var ss) ? ss : Dt;
        IntPtr sim = heft_sim_new(w, two ? 1 : 0, substep);
        var poses = new double[n * 7];
        var vels = new double[n * 6];
        var gp = new double[3];
        var gr = new double[4];
        var csv = new StringBuilder("t,cx,cy,cz,cqx,cqy,cqz,cqw,rx,ry,rz,rqx,rqy,rqz,rqw,ox,oy,oz,oqx,oqy,oqz,oqw\n");
        int steps = (int)Math.Round(heft_path_duration(p) / Dt);
        for (int k = 0; k <= steps; ++k)
        {
            double t = k * Dt;
            for (int i = 0; i < n; ++i)
            {
                Pose(p, w, i, t, out var cp, out var cr);
                // HVRJointHand.UpdateTargetVelocity: the controller's own velocity, in the wrist's frame
                var vel = (cp - lastPos[i]) / (float)Dt;
                (cr * Quaternion.Inverse(lastRot[i])).ToAngleAxis(out float ang, out Vector3 axis);
                if (ang > 180) ang -= 360;
                var angVel = float.IsFinite(axis.x) ? axis * (ang * Mathf.Deg2Rad / (float)Dt) : Vector3.zero;
                // Hurricane's wrist is a child of the controller: it is carried by the
                // hierarchy, so PhysX sees it teleported, at rest - not moved with a velocity.
                wrists[i].position = cp;
                wrists[i].rotation = cr;
                handJoints[i].targetVelocity = Quaternion.Inverse(cr) * vel;
                handJoints[i].targetAngularVelocity = Quaternion.Inverse(cr) * angVel;
                double[] pv = { cp.x, cp.y, cp.z, cr.x, cr.y, cr.z, cr.w };
                Array.Copy(pv, 0, poses, i * 7, 7);
                double[] vv = { vel.x, vel.y, vel.z, angVel.x, angVel.y, angVel.z };
                Array.Copy(vv, 0, vels, i * 6, 6);
                lastPos[i] = cp;
                lastRot[i] = cr;
            }
            if (k > 0) Physics.Simulate((float)Dt);
            heft_sim_step(sim, poses, vels, k > 0 ? Dt : 0);
            heft_sim_grip(sim, gp, gr);
            Pose(p, w, 0, t, out var c, out var cq);
            Vector3 rp = wb.position;
            Quaternion rq = wb.rotation;
            csv.Append(string.Join(",", new double[] { t, c.x, c.y, c.z, cq.x, cq.y, cq.z, cq.w, rp.x, rp.y, rp.z, rq.x, rq.y, rq.z, rq.w,
                                                         gp[0], gp[1], gp[2], gr[0], gr[1], gr[2], gr[3] }
                .Select(x => x.ToString("R", CultureInfo.InvariantCulture)))).Append('\n');
        }
        heft_sim_free(sim);
        File.WriteAllText(Path.Combine(outDir, $"{wName}_{pName}.csv"), csv.ToString());
    }
}
