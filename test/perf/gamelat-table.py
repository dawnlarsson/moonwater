"""gamelat-table.py FILE.jsonl...: gamelat rows as one table line each (gameinput)."""
import json, sys

for name in sys.argv[1:]:
    for line in open(name):
        r = json.loads(line)
        if "rate" not in r and "what" not in r:
            print(r.get("label"), r.get("case"), "HEAD", {k: r.get(k) for k in ("locked", "keys_focus", "grab", "focus")})
            continue
        if r.get("what") == "keys":
            print("%-6s %-12s keys %d of %d, inject->client us %s" % (r.get("label"), r.get("case"), r["came"], r["sent"], r["inject_to_client_us"]))
            continue
        l = r["inject_to_client_us"]
        d = r.get("device_stamp_to_client_us") or {}
        m = r.get("motion_inject_to_client_us") or {}
        print("%-6s %-12s %5d Hz sent %6d ev %6d unaccel %6d/%6d acc %8s merged %5d frames %6s motions %6s dups %5s | inject p50 %6s p99 %7s max %7s"
              " | dev p50 %6s p99 %7s | motion p50 %s p99 %s" % (
                  r.get("label"), r.get("case"), r["rate"], r["sent"], r["events"], r["came_unaccel"], r["sent_counts"], r["came_accel"],
                  r["merged"], r.get("frames"), r.get("motions"), r.get("sdl_dups_dropped"), l.get("p50"), l.get("p99"), l.get("max"),
                  d.get("p50"), d.get("p99"), m.get("p50"), m.get("p99")))
