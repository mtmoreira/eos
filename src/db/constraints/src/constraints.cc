// constraints: constraint objects bound to design objects (L2).
#include "eos/db/constraints.h"

#include <utility>

namespace eos::db {

void ConstraintSet::Resolve(Constraint& constraint, const Design& design) {
  constraint.bound_object = ObjectId();
  if (constraint.target_spec.empty()) {
    constraint.status = BindingStatus::kEmpty;
    return;
  }
  // Pin paths name a pin directly ("<instance>/<pin>").
  if (constraint.target_spec.find('/') != std::string::npos) {
    const ObjectId pin =
        design.Find(foundation::ObjectKind::kPin, constraint.target_spec);
    if (!pin.is_null()) {
      constraint.bound_object = pin;
      constraint.status = BindingStatus::kBound;
      return;
    }
    constraint.status = BindingStatus::kUnresolved;
    return;
  }
  // Plain names resolve among ports, then nets, then instances.
  for (const foundation::ObjectKind kind :
       {foundation::ObjectKind::kPort, foundation::ObjectKind::kNet,
        foundation::ObjectKind::kInstance}) {
    const ObjectId found = design.Find(kind, constraint.target_spec);
    if (!found.is_null()) {
      constraint.bound_object = found;
      constraint.status = BindingStatus::kBound;
      return;
    }
  }
  constraint.status = BindingStatus::kUnresolved;
}

ConstraintId ConstraintSet::Add(ConstraintKind kind, const std::string& name,
                                const std::string& target_spec,
                                const Design& design) {
  Constraint constraint;
  constraint.id = next_id_++;
  constraint.kind = kind;
  constraint.name = name;
  constraint.target_spec = target_spec;
  Resolve(constraint, design);
  constraints_.push_back(std::move(constraint));
  return constraints_.back().id;
}

void ConstraintSet::RebindAll(const Design& design) {
  for (Constraint& constraint : constraints_) {
    Resolve(constraint, design);
  }
}

const Constraint* ConstraintSet::Find(ConstraintId id) const {
  for (const Constraint& constraint : constraints_) {
    if (constraint.id == id) {
      return &constraint;
    }
  }
  return nullptr;
}

CoverageReport ConstraintSet::Coverage() const {
  CoverageReport report;
  report.total = constraints_.size();
  for (const Constraint& constraint : constraints_) {
    switch (constraint.status) {
      case BindingStatus::kBound: report.bound += 1; break;
      case BindingStatus::kUnresolved: report.unresolved += 1; break;
      case BindingStatus::kEmpty: report.empty += 1; break;
    }
  }
  return report;
}

std::vector<const Constraint*> ConstraintSet::Unresolved() const {
  std::vector<const Constraint*> out;
  for (const Constraint& constraint : constraints_) {
    if (constraint.status == BindingStatus::kUnresolved) {
      out.push_back(&constraint);
    }
  }
  return out;
}

}  // namespace eos::db
