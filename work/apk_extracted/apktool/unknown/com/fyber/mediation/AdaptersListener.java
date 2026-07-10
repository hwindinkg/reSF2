package com.fyber.mediation;

import java.lang.Object;
import java.lang.String;
import java.util.Map;
import java.util.Set;

public interface AdaptersListener {
  void startedAdapters(Set<String> adapters, Map<String, Map<String, Object>> configs);
}
