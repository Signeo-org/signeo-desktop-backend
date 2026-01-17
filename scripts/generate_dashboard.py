import os
import xml.etree.ElementTree as ET
import glob

def parse_junit_xml(file_path):
    if not os.path.exists(file_path):
        return {"tests": 0, "failures": 0, "errors": 0, "time": 0.0, "cases": []}
    
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        stats = {
            "tests": int(root.attrib.get("tests", 0)),
            "failures": int(root.attrib.get("failures", 0)),
            "errors": int(root.attrib.get("errors", 0)),
            "time": float(root.attrib.get("time", 0.0)),
            "cases": []
        }
        
        for case in root.findall(".//testcase"):
            tc_name = case.attrib.get("name", "unknown")
            tc_class = case.attrib.get("classname", "unknown")
            tc_time = case.attrib.get("time", "0.0")
            failure = case.find("failure")
            status = "FAIL" if failure is not None else "PASS"
            message = failure.attrib.get("message", "") if failure is not None else ""
            
            stats["cases"].append({
                "name": tc_name,
                "classname": tc_class,
                "time": tc_time,
                "status": status,
                "message": message
            })
            
        return stats
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")
        return {"tests": 0, "failures": 0, "errors": 0, "time": 0.0, "cases": []}

def parse_benchmark_xml(file_path):
    if not os.path.exists(file_path):
        return []

    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        benchmarks = []
        
        for case in root.findall(".//testcase"):
            name = case.attrib.get("name", "unknown")
            category = case.attrib.get("classname", "unknown")
            metrics = {}
            for prop in case.findall(".//property"):
                metrics[prop.attrib["name"]] = prop.attrib["value"]
            
            benchmarks.append({
                "name": name,
                "category": category,
                "metrics": metrics
            })
            
        return benchmarks
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")
        return []

def generate_html(unit_stats, integration_stats, benchmarks, output_file):
    total_tests = unit_stats["tests"] + integration_stats["tests"]
    total_failures = unit_stats["failures"] + integration_stats["failures"]
    total_errors = unit_stats["errors"] + integration_stats["errors"]
    status_color = "red" if total_failures + total_errors > 0 else "green"
    status_text = "FAILED" if total_failures + total_errors > 0 else "PASSED"

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Test & Coverage Dashboard</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f4f6f8; color: #333; margin: 0; padding: 20px; }}
        .container {{ max-width: 1200px; margin: 0 auto; }}
        header {{ background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; display: flex; justify-content: space-between; align-items: center; }}
        h1 {{ margin: 0; font-size: 24px; }}
        .status-badge {{ padding: 8px 16px; border-radius: 20px; color: white; font-weight: bold; background: {status_color}; }}
        .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 20px; margin-bottom: 20px; }}
        .card {{ background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
        .card h2 {{ margin-top: 0; border-bottom: 1px solid #eee; padding-bottom: 10px; }}
        .stat {{ font-size: 32px; font-weight: bold; margin: 10px 0; }}
        .stat-label {{ color: #666; font-size: 14px; text-transform: uppercase; letter-spacing: 0.5px; }}
        table {{ width: 100%; border-collapse: collapse; margin-top: 10px; }}
        th, td {{ padding: 10px; text-align: left; border-bottom: 1px solid #eee; }}
        th {{ background: #f9fafb; font-weight: 600; font-size: 12px; text-transform: uppercase; color: #666; }}
        .pass {{ color: green; font-weight: bold; }}
        .fail {{ color: red; font-weight: bold; }}
        .btn {{ display: inline-block; padding: 10px 20px; background: #007bff; color: white; text-decoration: none; border-radius: 4px; font-weight: 500; text-align: center; }}
        .btn:hover {{ background: #0056b3; }}
        .coverage-link {{ display: block; margin-top: 20px; text-align: center; font-size: 18px; }}
        pre {{ background: #f4f4f4; padding: 10px; border-radius: 4px; overflow-x: auto; font-size: 12px; }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Signeo Backend Test Report</h1>
            <span class="status-badge" style="background: {'#dc3545' if total_failures > 0 else '#28a745'}">{status_text}</span>
        </header>

        <div class="grid">
            <div class="card">
                <h2>Execution Summary</h2>
                <div style="display: flex; justify-content: space-around; text-align: center;">
                    <div>
                        <div class="stat">{total_tests}</div>
                        <div class="stat-label">Total Tests</div>
                    </div>
                    <div>
                        <div class="stat" style="color: {'#dc3545' if total_failures > 0 else '#28a745'}">{total_failures + total_errors}</div>
                        <div class="stat-label">Failures</div>
                    </div>
                </div>
                <div class="coverage-link">
                     <a href="/coverage/index.html" class="btn">View Full Coverage Report</a>
                </div>
            </div>

            <div class="card">
                <h2>Test Suites</h2>
                <table>
                    <thead><tr><th>Suite</th><th>Tests</th><th>Failures</th><th>Time</th></tr></thead>
                    <tbody>
                        <tr>
                            <td>Unit Tests</td>
                            <td>{unit_stats['tests']}</td>
                            <td class="{ 'fail' if unit_stats['failures'] > 0 else 'pass' }">{unit_stats['failures']}</td>
                            <td>{unit_stats['time']:.2f}s</td>
                        </tr>
                        <tr>
                            <td>Integration Tests</td>
                            <td>{integration_stats['tests']}</td>
                            <td class="{ 'fail' if integration_stats['failures'] > 0 else 'pass' }">{integration_stats['failures']}</td>
                            <td>{integration_stats['time']:.2f}s</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </div>

        <div class="card" style="margin-bottom: 20px;">
            <h2>Benchmarks</h2>
            <table>
                <thead><tr><th>Name</th><th>Category</th><th>Primary Metric</th><th>Value</th></tr></thead>
                <tbody>
    """

    for bench in benchmarks:
        metric_name = "N/A"
        metric_val = "N/A"
        
        m = bench["metrics"]
        if "throughput_ops_sec" in m:
            metric_name = "Throughput"
            metric_val = f"{float(m['throughput_ops_sec']):.0f} ops/s"
        elif "rtf" in m:
            metric_name = "RTF"
            metric_val = f"{float(m['rtf']):.4f}"
        elif "wer_percentage" in m:
            metric_name = "WER"
            metric_val = f"{float(m['wer_percentage']):.1f}%"
        elif "simulated_chars" in m:
             metric_name = "Throughput"
             metric_val = f"{float(m['throughput_chars_sec']):.0f} char/s"
        
        html += f"""
                    <tr>
                        <td>{bench['name']}</td>
                        <td>{bench['category']}</td>
                        <td>{metric_name}</td>
                        <td style="font-family: monospace;">{metric_val}</td>
                    </tr>
        """

    html += """
                </tbody>
            </table>
        </div>

        <div class="card">
            <h2>Detailed Failures</h2>
    """
    
    if total_failures + total_errors == 0:
        html += "<p style='color: green; text-align: center; padding: 20px;'>No failures detected. Great job!</p>"
    else:
        for stats in [unit_stats, integration_stats]:
            for case in stats["cases"]:
                if case["status"] == "FAIL":
                    html += f"""
                    <div style="border-left: 4px solid red; padding-left: 10px; margin-bottom: 10px;">
                        <strong>{case['classname']}::{case['name']}</strong>
                        <pre>{case['message']}</pre>
                    </div>
                    """

    html += """
        </div>
    </div>
</body>
</html>
    """

    with open(output_file, "w") as f:
        f.write(html)
    print(f"Dashboard generated at: {output_file}")

if __name__ == "__main__":
    results_dir = "test_results"
    unit_xml = os.path.join(results_dir, "unit_tests_report.xml")
    integration_xml = os.path.join(results_dir, "integration_tests_report.xml")
    benchmark_xml = os.path.join(results_dir, "benchmark_results.xml")
    
    print("Parsing test results...")
    unit = parse_junit_xml(unit_xml)
    integration = parse_junit_xml(integration_xml)
    benchmarks = parse_benchmark_xml(benchmark_xml)
    
    output_html = os.path.join(results_dir, "index.html")
    generate_html(unit, integration, benchmarks, output_html)
